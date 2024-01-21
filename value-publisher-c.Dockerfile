ARG BASEIMAGE=gcc:latest
FROM ${BASEIMAGE}

WORKDIR /app

# dependencies
RUN apt update \
    && apt install -y -q dnsutils \
    && apt autoremove -y -q \
    && apt clean -y -q \
    && rm -rf /var/lib/apt/lists/*

# application
COPY value-publisher.c /app
RUN gcc -o value-publisher -lm value-publisher.c

STOPSIGNAL SIGINT
ENTRYPOINT /app/value-publisher $(dig +short telegraf)
