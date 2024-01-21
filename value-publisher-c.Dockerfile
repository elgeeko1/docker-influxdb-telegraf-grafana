ARG BASEIMAGE=gcc:latest
FROM ${BASEIMAGE}

WORKDIR /app

# application
COPY value-publisher.c /app
RUN gcc -o value-publisher -lm value-publisher.c

STOPSIGNAL SIGINT
ENTRYPOINT ["/app/value-publisher"]
