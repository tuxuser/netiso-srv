FROM alpine:latest

WORKDIR /app
COPY netiso-srv .
RUN chmod +x ./netiso-srv

VOLUME /mnt
EXPOSE 4323/TCP

# Quick smoke test
RUN /app/netiso-srv -h

# Wildcard bind inside the container; the port publish decides who can reach it.
CMD ["/app/netiso-srv", "-r", "-b", "0.0.0.0", "/mnt"]
