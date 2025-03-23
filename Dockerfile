FROM alpine:latest

WORKDIR /app
COPY netiso-srv .
RUN chmod +x ./netiso-srv

VOLUME /mnt
EXPOSE 4323/TCP

CMD ["/app/netiso-srv", "-r", "/mnt"]
