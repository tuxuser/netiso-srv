# NetISO server daemon

## Usage

Run: `netiso-srv [directory with *.iso files]`


## Docker

To build the docker image

```
docker build -t netiso:local .
docker run -v /path/to/isos:/mnt netiso:local
```

See `Dockerfile` and use `musl`-binary of `netiso-srv`.
