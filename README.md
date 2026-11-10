[![Build](https://github.com/tuxuser/netiso-srv/actions/workflows/build.yml/badge.svg)](https://github.com/tuxuser/netiso-srv/actions/workflows/build.yml)
[![Docker image tags](https://ghcr-badge.egpl.dev/tuxuser/netiso-srv-rs/tags?color=%2344cc11&ignore=latest&n=3&label=image+tags&trim=)](https://github.com/tuxuser/netiso-srv/pkgs/container/netiso-srv-rs)
[![GitHub Release](https://img.shields.io/github/v/release/tuxuser/netiso-srv)](https://github.com/tuxuser/netiso-srv/releases/latest)

# NetISO server daemon

RUST rewrite of the NetISO server. Original plugin + server: <https://github.com/XeFreedom/NetISO>

Thanks cOz!

## Usage

Options:

    `-r` - Recursive scanning for ISO files
    `-b <address>` - Listen on this address (default `127.0.0.1`)
    `-p` - Pace replies to 94 Mbit/s (Linux only, see below)
    `-v` - Verbose output
    `-h` - Print usage

Run: `netiso-srv [-r] [-p] [-v] [-h] [-b <address>] [directory with *.iso files]`

The protocol has no authentication: every host that can reach port 4323 can
read every ISO in the directory. The server therefore listens on loopback
unless told otherwise. Give it the NAS address the console talks to:

```
netiso-srv -r -b 192.168.1.10 /path/to/isos
```

and, if the NAS has a firewall, allow port 4323 only from the console's IP.
Never forward the port to the internet.

### Latency notes

The console plugin reads 4 KiB per round trip and waits for each reply, so
server turnaround sits on every read. The server keeps that path short:

- one blocking thread per console, positional reads (`pread`), no async hops
- `TCP_NODELAY`, so the tail of a reply is never held back for an ACK
- `write_all`, so a reply is never truncated (a short reply stalls the console
  for 15 s and drops the disc)
- a 1 MiB `posix_fadvise(WILLNEED)` window ahead of the read position, so HDD
  seeks are paid once per MiB instead of once per region

`-p` caps the send rate at 94 Mbit/s of payload, which is what a 100 Mbit port
carries once frame overhead is counted (1460 of every 1538 wire bytes). It
stops a gigabit NAS from overflowing the switch buffer in front of the console,
which costs a 200 ms retransmit per dropped packet. With the stock plugin's
4 KiB replies the buffer can never overflow, so leave `-p` off unless the
plugin is changed to larger reads or `ss -ti` shows retransmits.

## Docker

The container binds `0.0.0.0` on its own network; the port publish is the
boundary. Publish on the NAS LAN address only, not on all interfaces.

Spawn container standalone
```
docker run -p 192.168.1.10:4323:4323 -v /path/to/isos:/mnt ghcr.io/tuxuser/netiso-srv-rs:latest
```

or

Spawn via docker compose
```
NAS_LAN_IP=192.168.1.10 docker compose up
```

For the lowest latency use host networking instead of a published port
(`--network host`), and then pass `-b <NAS LAN IP>` as the bind address.

## Client (Xbox 360 plugin)

The console side, `netiso.xex`, lives in [`client/`](client/) with its own
README. It builds only with the Microsoft Xbox 360 SDK on Windows, so the
GitHub Actions workflows here cover the server alone.

## Development

### Build

```
cargo build [--release]
```

### Build docker image locally

Make sure to have the `netiso-srv` built and available in current directory.

Build the image:

```
docker build -t netiso:localdev .
```

The resulting docker image is now ready-to-use from `netiso:localdev`, see `Docker`-steps above for regular docker-usage.

## Forks

- [by Xeroxxx](https://github.com/Xeroxxx/netiso-srv) - ZISO server-side support
