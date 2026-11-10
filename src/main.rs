#![allow(clippy::upper_case_acronyms)]

use binrw::{BinRead, BinWrite};
use glob::glob;
use std::env;
use std::error::Error;
use std::ffi::OsStr;
use std::fs::File;
use std::io::{self, Cursor, Read, Write};
use std::net::{IpAddr, Ipv4Addr, TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::thread;

const NETISO_SRV_PORT: u16 = 4323;
const SECTOR_SIZE: u16 = 0x800; // 2048
const XGD_MAGIC: &[u8; 20] = b"MICROSOFT*XBOX*MEDIA";
/// Wire size of one request header: magic + cmd + index + offset + length.
const MESSAGE_SIZE: usize = 20;
/// How far ahead of the console's read position the kernel is asked to pull
/// the ISO into the page cache. The plugin reads 4 KiB per round trip, mostly
/// sequentially, so one hint covers ~256 requests and hides HDD seeks.
const READAHEAD_BYTES: u64 = 1024 * 1024;
/// Payload pacing cap for `-p`, in bytes/s (94 Mbit/s). The Xbox 360 port is
/// 100 Mbit and each 1460-byte payload rides in a 1538-byte wire frame (94.9 %),
/// so this keeps a gigabit NAS from overflowing the switch buffer in front of
/// the console. Only matters once replies are larger than a few KiB.
const PACING_RATE_BYTES_PER_SEC: u64 = 11_750_000;

#[derive(Debug)]
struct ActiveIso {
    file: File,
    metadata: IsoEntry,
    /// File offset up to which a read-ahead hint has already been issued.
    prefetched_until: u64,
}

#[derive(Clone, Debug)]
struct IsoEntry {
    path: PathBuf,
    filename: String,
    filesize: u64,
    data_start: u64,
    sector_count: u64,
    has_type1_file: u32,
}

#[derive(BinRead, BinWrite, Debug)]
#[brw(repr = u16)]
enum Cmd {
    Ping = 0,
    GetIsoSize = 1,
    HasType1File = 2,
    ReadData = 3,
    GetIsoName = 4,
    MountIso = 5,
}

// Message structure definition
#[derive(BinRead, BinWrite, Debug)]
#[brw(big, magic = b"ISVR")]
struct Message {
    cmd_type: Cmd,
    iso_index: u16,
    offset: u64,
    length: u32,
}

#[derive(Clone, Copy, Debug)]
struct Options {
    verbose: bool,
    pacing: bool,
    bind: IpAddr,
}

/// One connected console. The protocol is strictly request/response and the
/// plugin never has more than one command in flight, so a plain blocking
/// thread per connection is the shortest path from socket to disk and back.
struct Session {
    files: Arc<Vec<IsoEntry>>,
    active: Option<ActiveIso>,
    opts: Options,
    /// Reply buffer reused across requests, so the hot path does not allocate.
    buf: Vec<u8>,
}

/// Read exactly `buf.len()` bytes at `offset` without moving a file cursor.
#[cfg(unix)]
fn read_exact_at(file: &File, buf: &mut [u8], offset: u64) -> io::Result<()> {
    use std::os::unix::fs::FileExt;
    file.read_exact_at(buf, offset)
}

#[cfg(windows)]
fn read_exact_at(file: &File, buf: &mut [u8], offset: u64) -> io::Result<()> {
    use std::os::windows::fs::FileExt;
    let mut done = 0;
    while done < buf.len() {
        match file.seek_read(&mut buf[done..], offset + done as u64)? {
            0 => {
                return Err(io::Error::new(
                    io::ErrorKind::UnexpectedEof,
                    "read past end of ISO",
                ))
            }
            n => done += n,
        }
    }
    Ok(())
}

/// Ask the kernel to start loading `len` bytes at `offset` into the page cache.
/// POSIX_FADV_WILLNEED only queues the disk I/O and returns. No-op off Linux.
#[cfg(target_os = "linux")]
fn hint_willneed(file: &File, offset: u64, len: u64) {
    use std::os::unix::io::AsRawFd;
    // SAFETY: plain syscall on an open descriptor; no memory is handed over.
    unsafe {
        libc::posix_fadvise(
            file.as_raw_fd(),
            offset as libc::off_t,
            len as libc::off_t,
            libc::POSIX_FADV_WILLNEED,
        );
    }
}

#[cfg(not(target_os = "linux"))]
fn hint_willneed(_file: &File, _offset: u64, _len: u64) {}

/// Cap the send rate of this connection so a gigabit NAS cannot flood the
/// 100 Mbit switch port in front of the console (a dropped tail packet costs a
/// 200 ms retransmit stall). Linux counts TCP payload bytes here.
#[cfg(target_os = "linux")]
fn set_pacing_rate(stream: &TcpStream, bytes_per_sec: u64) -> io::Result<()> {
    use std::os::unix::io::AsRawFd;
    // SAFETY: setsockopt with a correctly sized, initialised value.
    let rc = unsafe {
        libc::setsockopt(
            stream.as_raw_fd(),
            libc::SOL_SOCKET,
            libc::SO_MAX_PACING_RATE,
            &bytes_per_sec as *const u64 as *const libc::c_void,
            std::mem::size_of::<u64>() as libc::socklen_t,
        )
    };
    if rc == 0 {
        Ok(())
    } else {
        Err(io::Error::last_os_error())
    }
}

#[cfg(not(target_os = "linux"))]
fn set_pacing_rate(_stream: &TcpStream, _bytes_per_sec: u64) -> io::Result<()> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "SO_MAX_PACING_RATE is Linux only",
    ))
}

fn get_data_start(file: &File) -> Result<u64, Box<dyn Error>> {
    let offsets: [(u64, u64); 3] = [
        // XGD2 / GDF
        (0xfda0000, 0xfd90000),
        // XGD3
        (0x2090000, 0x2080000),
        // XSF
        (0x10000, 0x0),
    ];

    let len = file.metadata()?.len();
    let mut buf = [0u8; XGD_MAGIC.len()];
    for (offset, data_start) in offsets {
        if len >= offset + XGD_MAGIC.len() as u64 {
            read_exact_at(file, &mut buf, offset)?;
            if &buf == XGD_MAGIC {
                return Ok(data_start);
            }
        }
    }

    // No XGD Magic found in expected offsets
    // Assume data starts @ 0x0
    Ok(0)
}

fn get_iso_files(
    old_entries: &[IsoEntry],
    directory: &Path,
    recursive: bool,
) -> Result<Vec<IsoEntry>, Box<dyn Error>> {
    let mut ret = old_entries.to_vec();

    // First, throw out obsolete entries
    ret.retain(|x| x.path.exists());

    // Assemble glob pattern
    let isofiles_glob_pattern = {
        let mut path_glob = directory.to_str().ok_or("directory path is not UTF-8")?.to_string();
        if recursive {
            path_glob += std::path::MAIN_SEPARATOR_STR;
            path_glob += "**"
        }

        path_glob += std::path::MAIN_SEPARATOR_STR;
        path_glob += "*.[iI][sS][oO]"; // This matches .iso, .ISO, .Iso, .iSO, etc.

        path_glob
    };

    // Search for new files
    let files: Vec<PathBuf> = glob(&isofiles_glob_pattern)?
        .filter_map(|x| x.ok())
        // Filter for existing files
        .filter(|x| x.is_file())
        // Filter for new files (PathBuf not identical to any previous entry)
        .filter(|x| !ret.iter().any(|y| y.path == *x))
        .collect();

    for filepath in files {
        let filesize = filepath.metadata()?.len();
        let filename = filepath
            .file_name()
            .unwrap_or(OsStr::new(""))
            .to_str()
            .unwrap_or("")
            .to_string();
        let handle = File::open(&filepath)?;
        let data_start = match get_data_start(&handle) {
            Ok(data_start) => data_start,
            Err(err) => {
                eprintln!("Invalid iso file: {filepath:?}, err: {err:?}");
                continue;
            }
        };

        let entry = IsoEntry {
            path: filepath.clone(),
            filename,
            filesize,
            data_start,
            sector_count: filesize / SECTOR_SIZE as u64,
            has_type1_file: 0,
        };

        ret.push(entry);
    }

    Ok(ret)
}

impl ActiveIso {
    /// Keep READAHEAD_BYTES of the ISO queued ahead of the console's position.
    /// Sequential reads re-arm every half window; a jump re-arms at the new spot.
    fn prefetch_after(&mut self, pos: u64) {
        let window = READAHEAD_BYTES;
        let inside = pos <= self.prefetched_until && pos + window >= self.prefetched_until;
        let near_edge = pos + window / 2 >= self.prefetched_until;
        if inside && !near_edge {
            return;
        }
        let start = if inside { self.prefetched_until } else { pos };
        if start >= self.metadata.filesize {
            return;
        }
        hint_willneed(&self.file, start, window);
        self.prefetched_until = start + window;
    }
}

impl Session {
    fn new(files: Arc<Vec<IsoEntry>>, opts: Options) -> Self {
        Session {
            files,
            active: None,
            opts,
            buf: Vec::new(),
        }
    }

    fn unmount(&mut self) {
        self.active = None;
    }

    /// Returns the protocol result code: 1 mounted, 0 failed.
    fn mount(&mut self, normalized: &str) -> u32 {
        let found = self.files.iter().find(|x| x.filename.ends_with(normalized));
        match found {
            Some(iso) => match File::open(&iso.path) {
                Ok(file) => {
                    println!("Mounting: {:?} (data_start 0x{:x})", iso.path, iso.data_start);
                    self.active = Some(ActiveIso {
                        file,
                        metadata: iso.clone(),
                        prefetched_until: 0,
                    });
                    1
                }
                Err(err) => {
                    eprintln!("MountIso: Failed to open '{:?}': {err}", iso.path);
                    0
                }
            },
            None => {
                eprintln!("MountIso: Failed to find ISO '{normalized}' !");
                0
            }
        }
    }

    /// Serve one console until it disconnects.
    fn run(&mut self, mut socket: TcpStream) -> Result<(), Box<dyn Error>> {
        let peer = socket.peer_addr()?;
        let mut header = [0u8; MESSAGE_SIZE];
        loop {
            // A 20-byte command may arrive split; read_exact waits for all of it.
            if let Err(err) = socket.read_exact(&mut header) {
                if err.kind() == io::ErrorKind::UnexpectedEof {
                    eprintln!("EOF - Client '{peer}' disconnected");
                    return Ok(());
                }
                return Err(err.into());
            }

            let msg = Message::read(&mut Cursor::new(&header))?;
            if self.opts.verbose {
                println!("< {msg:?}");
            }

            // write_all, never try_write: a partial reply would leave the
            // console waiting 15 s for bytes that never come.
            match msg.cmd_type {
                Cmd::Ping => socket.write_all(b"ISVRokOK")?,
                Cmd::GetIsoSize => {
                    // Sector count and sector size of the mounted ISO; all
                    // zeros when nothing is mounted, like the original server.
                    let mut resp = [0u8; 8];
                    if let Some(active) = &self.active {
                        let sectors = active.metadata.sector_count as u32;
                        resp[..4].copy_from_slice(&sectors.to_be_bytes());
                        resp[4..].copy_from_slice(&(SECTOR_SIZE as u32).to_be_bytes());
                    }
                    socket.write_all(&resp)?;
                }
                Cmd::HasType1File => {
                    let flag = self
                        .files
                        .get(msg.iso_index as usize)
                        .map_or(0, |iso| iso.has_type1_file);
                    socket.write_all(&flag.to_be_bytes())?;
                }
                Cmd::ReadData => {
                    let len = msg.length as usize;
                    // Closing the connection makes the plugin mark the disc
                    // unavailable at once instead of after its 15 s timeout.
                    let active = self
                        .active
                        .as_mut()
                        .ok_or("ReadData with no ISO mounted")?;
                    self.buf.resize(len, 0);
                    read_exact_at(&active.file, &mut self.buf[..len], msg.offset)?;
                    socket.write_all(&self.buf[..len])?;
                    active.prefetch_after(msg.offset + len as u64);
                }
                Cmd::GetIsoName => {
                    let len = msg.length as usize;
                    let name = self
                        .files
                        .get(msg.iso_index as usize)
                        .map_or("", |iso| iso.filename.as_str());
                    self.buf.clear();
                    self.buf.extend_from_slice(name.as_bytes());
                    // The request carries the client's buffer size: pad or cut to exactly that.
                    self.buf.resize(len, 0);
                    socket.write_all(&self.buf[..len])?;
                }
                Cmd::MountIso => {
                    let mut iso_name = vec![0u8; msg.length as usize];
                    socket.read_exact(&mut iso_name)?;

                    let iso_name_human = String::from_utf8_lossy(&iso_name).into_owned();
                    let normalized = iso_name_human
                        .replace("\\Mount", "")
                        .replace(['\\', '\0'], "");

                    println!("Normalized ISO Name: {iso_name_human} -> {normalized}");

                    let code: u32 = if normalized == "[Disable Current ISO]" {
                        println!("Unmounting current iso...");
                        self.unmount();
                        0
                    } else {
                        self.mount(&normalized)
                    };
                    socket.write_all(&code.to_be_bytes())?;
                }
            }
        }
    }
}

fn tune_socket(socket: &TcpStream, opts: Options) {
    // Nagle would hold the tail segment of every reply until the console ACKs
    // the segments before it. The plugin waits for the whole reply before it
    // sends anything, so that is one LAN round trip per 4 KiB read for nothing.
    if let Err(err) = socket.set_nodelay(true) {
        eprintln!("set_nodelay failed: {err}");
    }
    if opts.pacing {
        match set_pacing_rate(socket, PACING_RATE_BYTES_PER_SEC) {
            Ok(()) => println!("Pacing replies at {PACING_RATE_BYTES_PER_SEC} bytes/s"),
            Err(err) => eprintln!("Pacing not applied: {err}"),
        }
    }
}

fn print_usage(bin_name: &str) {
    eprintln!("Usage: {} [-rvph] [-b <address>] [iso directory path]", bin_name);
    eprintln!("\nArgs:");
    eprintln!("\t-r - Recursive ISO scanning");
    eprintln!("\t-b <address> - Listen on this address (default 127.0.0.1; use the NAS LAN IP the Xbox talks to, or 0.0.0.0 inside a container)");
    eprintln!("\t-p - Pace replies to 94 Mbit/s so a gigabit NAS cannot overrun the console's 100 Mbit port (Linux only)");
    eprintln!("\t-v - Verbose output");
    eprintln!("\t-h - Print help / usage")
}

fn check_arg(args: &mut Vec<String>, arg_name: &str) -> bool {
    match args.iter().position(|x| arg_name == x) {
        Some(removal_index) => {
            args.remove(removal_index);
            true
        }
        None => false,
    }
}

/// Remove `-name <value>` from args and return the value, if present.
fn check_arg_value(args: &mut Vec<String>, arg_name: &str) -> Result<Option<String>, Box<dyn Error>> {
    match args.iter().position(|x| arg_name == x) {
        Some(index) => {
            if index + 1 >= args.len() {
                return Err(format!("{arg_name} needs a value").into());
            }
            args.remove(index);
            Ok(Some(args.remove(index)))
        }
        None => Ok(None),
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut args: Vec<String> = env::args().collect();

    let print_help = check_arg(&mut args, "-h"); // Help
    let recursive_scan = check_arg(&mut args, "-r"); // Recursive iso scanning
    let bind = match check_arg_value(&mut args, "-b")? {
        Some(addr) => addr.parse::<IpAddr>().map_err(|e| format!("-b {addr}: {e}"))?,
        None => IpAddr::V4(Ipv4Addr::LOCALHOST),
    };
    let opts = Options {
        verbose: check_arg(&mut args, "-v"), // Verbose / Debug
        pacing: check_arg(&mut args, "-p"),  // Rate-limit replies to the console's port speed
        bind,
    };

    if print_help {
        print_usage(&args[0]);
        return Ok(());
    } else if args.len() < 2 {
        eprintln!("ERROR: Invalid number of arguments!");
        print_usage(&args[0]);
        return Ok(());
    }

    let filepath = Path::new(&args[1]);

    println!("Enumerating ISOs in {filepath:?}...");
    let mut files = get_iso_files(&[], filepath, recursive_scan)?;

    if files.is_empty() {
        return Err("No iso files enumerated".into());
    }

    println!("Found the following ISOs");
    for (index, file) in files.iter().enumerate() {
        println!("{index}: {}", &file.filename);
    }

    let listener = TcpListener::bind((opts.bind, NETISO_SRV_PORT))?;
    println!("Listening on {}:{NETISO_SRV_PORT} ...", opts.bind);

    loop {
        let (socket, peer) = match listener.accept() {
            Ok(conn) => conn,
            Err(err) => {
                eprintln!("accept failed: {err}");
                continue;
            }
        };
        println!("Got connection from: {peer}");

        // Update list of isos
        files = get_iso_files(&files, filepath, recursive_scan)?;

        tune_socket(&socket, opts);

        let session_files = Arc::new(files.clone());
        thread::spawn(move || {
            let mut session = Session::new(session_files, opts);
            if let Err(err) = session.run(socket) {
                eprintln!("Connection handler exited with error: {err}");
            }
        });
    }
}
