ARG BASEIMAGE=python:latest
FROM ${BASEIMAGE}

# python configuration
ENV PYTHONDONTWRITEBYTECODE=1

WORKDIR /app

# python requirements
RUN pip install \
    --no-warn-script-location \
    --no-cache-dir \
    requests

# application
COPY value-publisher.py /app

STOPSIGNAL SIGINT
ENTRYPOINT ["python", "-u", "/app/value-publisher.py"]
