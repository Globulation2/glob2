# syntax=docker/dockerfile:1.7
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates g++ python3 scons pkg-config libsdl2-dev libsdl2-net-dev \
    libboost-dev libssl-dev zlib1g-dev libpcre3-dev libsdl2-ttf-dev libsdl2-image-dev \
    libvorbis-dev libogg-dev libspeex-dev libfribidi-dev libgl1-mesa-dev libglu1-mesa-dev libepoxy-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY . .
ARG JOBS=4
ARG TARGETARCH
RUN --mount=type=cache,id=glob2-server-build-${TARGETARCH},target=/source/build,sharing=locked scons role=server release=1 -j${JOBS} && \
    scons role=router release=1 -j${JOBS} && \
    mkdir -p /output && \
    cp build/linux/server/release/src/glob2-server build/linux/router/release/src/glob2-router /output/

FROM caddy:2.10.2-alpine AS proxy
RUN setcap -r /usr/bin/caddy && chown -R 10001:10001 /data /config
USER 10001:10001

FROM ubuntu:24.04 AS server
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates python3 libsdl2-2.0-0 libsdl2-net-2.0-0 libssl3t64 libpcre3 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home glob2 \
    && mkdir -p /var/lib/glob2 && chown glob2:glob2 /var/lib/glob2
COPY --from=build /output/glob2-server /usr/local/bin/
COPY --from=build /output/glob2-router /usr/local/bin/
COPY data /opt/glob2/data
COPY deploy/healthcheck.py /usr/local/bin/glob2-healthcheck
ENV GLOB2_USER_DATA_DIR=/var/lib/glob2
USER 10001:10001
WORKDIR /opt/glob2
CMD ["glob2-server"]
