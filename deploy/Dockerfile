FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates g++ python3 scons pkg-config libsdl2-dev libsdl2-image-dev \
    libsdl2-net-dev libsdl2-ttf-dev libvorbis-dev libogg-dev libspeex-dev \
    libboost-date-time-dev libboost-thread-dev libboost-system-dev zlib1g-dev \
    libfribidi-dev libpcre3-dev libgl1-mesa-dev libglu1-mesa-dev libepoxy-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY . .
ARG JOBS=4
RUN scons role=server release=1 -j${JOBS} && \
    scons role=router release=1 -j${JOBS} && \
    scons role=gateway release=1 -j${JOBS}

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates python3 libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-net-2.0-0 \
    libsdl2-ttf-2.0-0 libvorbisfile3 libspeex1 libboost-thread1.83.0 \
    libboost-date-time1.83.0 libboost-system1.83.0 libfribidi0 libpcre3 libepoxy0 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home glob2
COPY --from=build /source/build/linux/server/release/src/glob2-server /usr/local/bin/
COPY --from=build /source/build/linux/router/release/src/glob2-router /usr/local/bin/
COPY --from=build /source/build/linux/gateway/release/glob2-ws-gateway /usr/local/bin/
COPY data /opt/glob2/data
COPY deploy/healthcheck.py /usr/local/bin/glob2-healthcheck
USER glob2
WORKDIR /opt/glob2
CMD ["glob2-server"]
