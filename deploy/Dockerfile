FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates g++ make python3 scons pkg-config cmake libfreetype-dev libvorbis-dev libogg-dev libspeex-dev \
    libboost-dev libssl-dev zlib1g-dev \
    libfribidi-dev libpcre3-dev libgl1-mesa-dev libglu1-mesa-dev libepoxy-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY . .
ARG JOBS=4
ENV GLOB2_SDL3_PREFIX=/source/build/sdl3/prefix
RUN python3 scons/sdl3_dependencies.py --prefix "$GLOB2_SDL3_PREFIX" --work /source/build/sdl3/sources --jobs ${JOBS}
RUN scons role=server release=1 -j${JOBS} && \
    scons role=router release=1 -j${JOBS} && \
    scons role=gateway release=1 -j${JOBS}

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates python3 libfreetype6 libvorbisfile3 libspeex1 \
    libfribidi0 libpcre3 libepoxy0 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home glob2
COPY --from=build /source/build/linux/server/release/src/glob2-server /usr/local/bin/
COPY --from=build /source/build/linux/router/release/src/glob2-router /usr/local/bin/
COPY --from=build /source/build/linux/gateway/release/glob2-ws-gateway /usr/local/bin/
COPY --from=build /source/build/sdl3/prefix/lib/libSDL3* /usr/local/lib/glob2/
COPY --from=build /source/build/sdl3/prefix/share/licenses /usr/local/share/licenses
COPY data /opt/glob2/data
COPY deploy/healthcheck.py /usr/local/bin/glob2-healthcheck
USER glob2
WORKDIR /opt/glob2
CMD ["glob2-server"]
