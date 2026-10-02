# syntax=docker/dockerfile:1.7
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git g++ make python3 scons pkg-config cmake libfreetype-dev libvorbis-dev libogg-dev libspeex-dev \
    libboost-dev libssl-dev zlib1g-dev \
    libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev \
    libfribidi-dev libpcre3-dev libgl1-mesa-dev libglu1-mesa-dev libepoxy-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY . .
ARG JOBS=4
ARG TARGETARCH
ENV GLOB2_SDL3_PREFIX=/source/build/sdl3/prefix
RUN --mount=type=cache,id=glob2-server-build-${TARGETARCH},target=/source/build,sharing=locked \
    python3 scons/sdl3_dependencies.py --prefix "$GLOB2_SDL3_PREFIX" --work /source/build/sdl3/sources --jobs ${JOBS} && \
    scons role=server release=1 -j${JOBS} && \
    scons role=router release=1 -j${JOBS} && \
    mkdir -p /output && \
    cp build/linux/server/release/src/glob2-server build/linux/router/release/src/glob2-router /output/ && \
    cp -a build/sdl3/prefix/lib/libSDL3* /output/ && \
    cp -a build/sdl3/prefix/share/licenses /output/licenses

FROM caddy:2.11.4-alpine AS proxy
RUN setcap -r /usr/bin/caddy && chown -R 10001:10001 /data /config
USER 10001:10001

FROM ubuntu:24.04 AS server
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates python3 libssl3t64 zlib1g libfreetype6 libvorbisfile3 libspeex1 \
    libfribidi0 libpcre3 libepoxy0 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home glob2 \
    && mkdir -p /var/lib/glob2 && chown glob2:glob2 /var/lib/glob2
COPY --from=build /output/glob2-server /usr/local/bin/
COPY --from=build /output/glob2-router /usr/local/bin/
COPY --from=build /output/libSDL3* /usr/local/lib/glob2/
COPY --from=build /output/licenses /usr/local/share/licenses
COPY data /opt/glob2/data
COPY deploy/healthcheck.py /usr/local/bin/glob2-healthcheck
ENV GLOB2_USER_DATA_DIR=/var/lib/glob2
USER 10001:10001
WORKDIR /opt/glob2
CMD ["glob2-server"]
