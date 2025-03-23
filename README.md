# NetISO server daemon

## Usage

Run: `netiso-srv [directory with *.iso files]`


## Docker

To build the docker image

```
docker build -t netiso:local .
```

Spawn container standalone
```
docker run -p 4323:4323 -v /path/to/isos:/mnt netiso:local
```

or

Spawn via docker compose
```
docker compose up
```

See `Dockerfile` and use `musl`-binary of `netiso-srv`.
