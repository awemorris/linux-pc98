FROM debian:13-slim

ARG DEBIAN_FRONTEND=noninteractive
ARG HOST_UID=1000
ARG HOST_GID=1000

COPY scripts/setup-packages.txt /tmp/setup-packages.txt
RUN sed -e 's/[[:space:]]*#.*$//' -e '/^[[:space:]]*$/d' \
        /tmp/setup-packages.txt > /tmp/packages.txt \
    && apt-get update \
    && xargs -r apt-get install --yes --no-install-recommends \
        < /tmp/packages.txt \
    && rm -rf /var/lib/apt/lists/* /tmp/packages.txt /tmp/setup-packages.txt \
    && groupadd --gid "${HOST_GID}" builder \
    && useradd --uid "${HOST_UID}" --gid "${HOST_GID}" \
        --create-home --shell /bin/bash builder \
    && printf 'builder ALL=(ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/builder \
    && chmod 0440 /etc/sudoers.d/builder

WORKDIR /work/linux-pc98
VOLUME ["/work/linux-pc98"]
USER builder
CMD ["./build.sh", "--help"]
