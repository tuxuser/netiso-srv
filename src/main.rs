use binrw::{BinRead, BinWrite};
use glob::glob;
use tokio::fs::File;
use std::env;
use std::error::Error;
use std::ffi::OsStr;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use tokio::io::{AsyncReadExt, AsyncSeekExt};
use tokio::net::TcpListener;

const NETISO_SRV_PORT: u16 = 4323;
const SECTOR_SIZE: u16 = 0x800; // 2048
const XGD_MAGIC: &[u8; 20] = b"MICROSOFT*XBOX*MEDIA";

#[derive(Debug)]
enum IsoType {
    XSF,
    XGD2,
    XGD3
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

#[derive(Default, Debug)]
struct Server {
    files: Vec<IsoEntry>,
    active_file: Option<File>,
}

async fn get_data_start(file: &mut File) -> Result<u64, Box<dyn std::error::Error>> {
    let offsets: [(u64, u64); 3] = [
        // XGD2 / GDF
        (0xfda0000, 0xfd90000),
        // XGD3
        (0x2090000, 0x2080000),
        // XSF
        (0x10000, 0x0)
    ];

    let len = file.metadata().await?.len();
    let mut buf = vec![0u8; XGD_MAGIC.len()];
    for (offset, data_start) in offsets {
        if len >= offset + XGD_MAGIC.len() as u64 {
            file.seek(std::io::SeekFrom::Start(offset)).await?;
            file.read_exact(&mut buf).await?;
            
            if buf == XGD_MAGIC {
                return Ok(data_start);
            }
        }
    }

    // No XGD Magic found in expected offsets
    // Assume data starts @ 0x0
    Ok(0)
}

async fn get_iso_files(directory: &Path) -> Result<Vec<IsoEntry>, Box<dyn std::error::Error>> {
    let mut ret = vec![];

    let isofiles_glob_pattern = directory.to_str().unwrap().to_string() + "/**/*.iso";
    println!("{isofiles_glob_pattern}");
    let files: Vec<PathBuf> = glob(&isofiles_glob_pattern)?
        .into_iter()
        .filter_map(|x| x.ok())
        .filter(|x|x.is_file())
        .collect();

    for filepath in files {
        let filesize = filepath.metadata()?.len();
        let filename = filepath.file_name()
            .unwrap_or(OsStr::new(""))
            .to_str()
            .unwrap_or("")
            .to_string();
        let mut handle = File::open(&filepath).await?;
        let data_start =  match get_data_start(&mut handle).await {
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

impl Server {
    async fn disable_current_iso(&mut self) {
        if self.active_file.is_some() {
            self.active_file = None;
        }
    }

    async fn handle_connection(&mut self, mut socket: tokio::net::TcpStream) -> Result<(), Box<dyn std::error::Error>> {
        loop {
            let mut buffer = [0; 20];
            match socket.read(&mut buffer).await {
                Ok(size) => {
                    if size <= 0 {
                        eprintln!("EOF - Client disconnected");
                        break
                    }

                    let mut cur = Cursor::new(&buffer);
                    let msg = Message::read(&mut cur)?;

                    match msg.cmd_type {
                        Cmd::Ping => {
                            let reply = "ISVRokOK".as_bytes();
                            socket.try_write(&reply)?;
                        },
                        Cmd::GetIsoSize => {
                            let maybe_iso = self.files.get(msg.iso_index as usize);
                            let sector_count = match maybe_iso {
                                Some(iso) => {
                                    iso.sector_count
                                },
                                None => {
                                    0
                                }
                            };

                            let mut resp = vec![];
                            resp.extend_from_slice(&(sector_count as u32).to_be_bytes());
                            resp.extend_from_slice(&(SECTOR_SIZE as u32).to_be_bytes());

                            socket.try_write(&resp)?;
                        },
                        Cmd::HasType1File => {
                            let maybe_iso = self.files.get(msg.iso_index as usize);
                            let has_type1_file = match maybe_iso {
                                Some(iso) => {
                                    iso.has_type1_file
                                },
                                None => {
                                    0
                                }
                            };

                            socket.try_write(&has_type1_file.to_be_bytes())?;
                        },
                        Cmd::ReadData => {
                            if let Some(file) = self.active_file.as_mut() {
                                let mut buf = vec![0u8; msg.length as usize];

                                file.seek(std::io::SeekFrom::Start(msg.offset)).await?;
                                file.read_exact(&mut buf).await?;

                                socket.try_write(&buf)?;
                            }
                        },
                        Cmd::GetIsoName => {
                            let maybe_iso = self.files.get(msg.iso_index as usize);
                            let filename = match maybe_iso {
                                Some(iso) => {
                                    &iso.filename
                                },
                                None => {
                                    ""
                                }
                            };
                            let mut response = filename.as_bytes().to_vec();
                            // The request contains the expected bytecount, so we extend the slice here
                            response.resize(msg.length as usize, 0);

                            socket.try_write(&response)?;
                        },
                        Cmd::MountIso => {
                            let mut iso_name = vec![0; msg.length as usize];
                            socket.read(&mut iso_name).await?;
                            let iso_name_human = String::from_utf8(iso_name)?;

                            let normalized = iso_name_human
                                .replace("\\Mount", "")
                                .replace("\\", "")
                                .replace("\x00", "");

                            println!("Normalized ISO Name: {iso_name_human} -> {normalized}");

                            if normalized == "[Disable Current ISO]" {
                                println!("Unmounting current iso...");
                                self.disable_current_iso().await;
                                let code = 1u32;
                                socket.try_write(&code.to_be_bytes())?;
                            } else {
                                let found = self.files.iter().find(|x| x.filename.ends_with(&normalized));
    
                                let code: u32 = match found {
                                    Some(iso) => {
                                        println!("Mounting: {:?}", iso.path);
                                        self.active_file = Some(File::open(&iso.path).await?);
                                        1 // success
                                    },
                                    None => {
                                        0 // error
                                    }
                                };
    
                                socket.try_write(&code.to_be_bytes())?;
                            }
                        },
                        _ => todo!()
                    }
                },
                Err(err) => {
                    eprintln!("Failed... {err}");
                }
            }
        }

        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();
    
    if args.len() < 2 {
        eprintln!("ERROR: Invalid number of arguments!");
        eprintln!("Usage: {} [iso directory path]", args[0]);
        return Ok(());
    }
    let filepath = Path::new(&args[1]);

    let files = get_iso_files(filepath).await?;

    if files.is_empty() {
        return Err("No iso files enumerated".into());
    }

    for (index, file) in files.iter().enumerate() {
        println!("{index}: {}", &file.filename);
    }

    let listener = TcpListener::bind(("0.0.0.0", NETISO_SRV_PORT)).await?;

    loop {
        let (socket, _) = listener.accept().await?;
        println!("Got connection from: {:?}", &socket.peer_addr());

        let files_clone = files.clone();
        tokio::spawn(async move {
            let mut srv = Server {
                files: files_clone,
                ..Default::default()
            };
            srv.handle_connection(socket).await.unwrap()
        });
    }

    Ok(())
}
