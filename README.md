[![Build](https://github.com/tuxuser/netiso-srv/actions/workflows/build.yml/badge.svg)](https://github.com/tuxuser/netiso-srv/actions/workflows/build.yml)
[![Docker image tags](https://ghcr-badge.egpl.dev/tuxuser/netiso-srv-rs/tags?color=%2344cc11&ignore=latest&n=3&label=image+tags&trim=)](https://github.com/tuxuser/netiso-srv/pkgs/container/netiso-srv-rs)
[![GitHub Release](https://img.shields.io/github/v/release/tuxuser/netiso-srv)](https://github.com/tuxuser/netiso-srv/releases/latest)

# NetISO server daemon

## Usage

Options:

    `-r` - Recursive scanning for ISO files
    `-h` - Print usage

Run: `netiso-srv [-r] [directory with *.iso files]`


## Docker

Spawn container standalone
```
docker run -p 4323:4323 -v /path/to/isos:/mnt ghcr.io/tuxuser/netiso-srv-rs:latest
```

or

Spawn via docker compose
```
docker compose up
```

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
