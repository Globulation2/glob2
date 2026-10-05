# syntax=docker/dockerfile:1.7
#
# Images for the self-hosted stack (deploy/compose.yaml).
#
#   target        image                         contents
#   platform      glob2-platform                platform-api, platform-worker and the operator CLI
#   engine-agent  glob2-engine-agent:<simver>   engine-agent + headless glob2 + data (one per sim version)
#   relay         glob2-relay                   glob2-relay
#   caddy         glob2-caddy                   Caddy + Caddyfile + built apps/web
#
# Build stages: source → cpp-base → relay-build; engine-source
# → engine-build; node-deps → web-build / platform-runtime-deps.

ARG NODE_IMAGE=node:22-bookworm-slim
ARG UBUNTU_IMAGE=ubuntu:24.04
ARG CADDY_IMAGE=caddy:2.11.4-alpine

# ---------------------------------------------------------------- C++ build

# The repository as a stage, so a build can swap in another source tree for the
# engine (see engine-source below).
FROM scratch AS source
COPY . /

FROM ${UBUNTU_IMAGE} AS cpp-deps
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git g++ make nasm python3 scons pkg-config cmake libfreetype-dev libopusfile-dev libopus-dev libogg-dev libspeex-dev \
    libboost-dev libssl-dev zlib1g-dev \
    libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev \
    libfribidi-dev libpcre3-dev libgl1-mesa-dev libglu1-mesa-dev libepoxy-dev \
    && rm -rf /var/lib/apt/lists/*
COPY deploy/source-identity.sh /usr/local/bin/source-identity
WORKDIR /source

FROM cpp-deps AS cpp-base
COPY --from=source / /source
RUN source-identity

FROM cpp-base AS relay-build
ARG JOBS=4
ARG TARGETARCH
# The relay links no SDL library but compiles against SDL3's headers (libgag
# types), so it builds the same pinned SDL3 prefix as the other C++ targets.
ENV GLOB2_SDL3_PREFIX=/source/build/sdl3/prefix
RUN --mount=type=cache,id=glob2-relay-build-${TARGETARCH},target=/source/build,sharing=locked \
    python3 scons/sdl3_dependencies.py --prefix "$GLOB2_SDL3_PREFIX" --work /source/build/sdl3/sources --jobs ${JOBS} && \
    scons role=relay release=1 -j${JOBS} build/linux/relay/release/src/glob2-relay && \
    mkdir -p /output && cp build/linux/relay/release/src/glob2-relay /output/ && \
    strip /output/glob2-relay

# The engine's source tree: this repository by default. To build an engine
# agent for another (older) sim version, pass that release's checkout:
#   docker buildx build --build-context engine-source=../glob2-old --target engine-agent …
FROM source AS engine-source

# The headless engine is the ordinary client binary run with SDL's dummy
# drivers. Its data/ is what the sim version's data hash covers, computed with
# this repository's deploy/sim_version.py.
FROM cpp-deps AS engine-build
ARG JOBS=4
ARG TARGETARCH
ARG GLOB2_SIM_VERSION=
COPY --from=engine-source / /source
COPY deploy/sim_version.py /usr/local/bin/glob2-sim-version
RUN source-identity
# The pinned SDL3 family is built into a prefix
# and shipped next to the binary, which finds it through $ORIGIN/../lib/glob2.
ENV GLOB2_SDL3_PREFIX=/source/build/sdl3/prefix
RUN --mount=type=cache,id=glob2-engine-build-${TARGETARCH},target=/source/build,sharing=locked \
    python3 scons/sdl3_dependencies.py --prefix "$GLOB2_SDL3_PREFIX" --work /source/build/sdl3/sources --jobs ${JOBS} && \
    scons release=1 server=0 -j${JOBS} build/linux/client/release/src/glob2 && \
    mkdir -p /output/bin /output/lib/glob2 /output/share && \
    cp build/linux/client/release/src/glob2 /output/bin/ && strip /output/bin/glob2 && \
    cp -a build/sdl3/prefix/lib/libSDL3* /output/lib/glob2/ && \
    cp -a build/sdl3/prefix/share/licenses /output/licenses && \
    cp -r data maps campaigns scripts /output/share/ && \
    find /output/share -name SConscript -delete && \
    python3 /usr/local/bin/glob2-sim-version /source --expect "${GLOB2_SIM_VERSION}" > /output/sim-version && \
    python3 /usr/local/bin/glob2-sim-version /source --hash-only > /output/sim-data-hash && \
    echo "sim version $(cat /output/sim-version)"

# ---------------------------------------------------------------- Node build

FROM ${NODE_IMAGE} AS node-deps
WORKDIR /app/platform
# Manifests first, so dependency layers survive source edits.
COPY platform/package.json platform/package-lock.json ./
COPY platform/packages/protocol/package.json packages/protocol/
COPY platform/packages/db/package.json packages/db/
COPY platform/packages/core/package.json packages/core/
COPY platform/packages/play/package.json packages/play/
COPY platform/packages/engine/package.json packages/engine/
COPY platform/packages/hive/package.json packages/hive/
COPY platform/packages/billing/package.json packages/billing/
COPY platform/packages/map-studio/package.json packages/map-studio/
COPY platform/packages/music/package.json packages/music/
COPY platform/apps/music-worker/package.json apps/music-worker/
COPY platform/apps/ai-map-worker/package.json apps/ai-map-worker/
COPY platform/apps/api/package.json apps/api/
COPY platform/apps/worker/package.json apps/worker/
COPY platform/apps/engine-agent/package.json apps/engine-agent/
COPY platform/apps/web/package.json apps/web/
RUN --mount=type=cache,id=glob2-npm,target=/root/.npm npm ci --no-audit --no-fund

FROM cpp-base AS music-web-build
RUN apt-get update && apt-get install -y --no-install-recommends autoconf automake libtool xz-utils && rm -rf /var/lib/apt/lists/*
RUN python3 tools/music/build_web.py

FROM node-deps AS web-build
# Optional links from the web app to a separately hosted public website and its
# download page (docs/hosting/README.md); empty keeps the defaults.
ARG GLOB2_WEBSITE_URL=
ARG GLOB2_DOWNLOAD_URL=
COPY platform/ ./
COPY --from=music-web-build /source/platform/apps/web/public/music/decoder.js apps/web/public/music/decoder.js
COPY --from=music-web-build /source/platform/apps/web/public/music/decoder.wasm apps/web/public/music/decoder.wasm
RUN VITE_WEBSITE_URL="$GLOB2_WEBSITE_URL" VITE_DOWNLOAD_URL="$GLOB2_DOWNLOAD_URL" \
    npm exec --workspace @glob2/web -- vite build

FROM node-deps AS platform-runtime-deps
RUN --mount=type=cache,id=glob2-npm,target=/root/.npm npm ci --omit=dev --no-audit --no-fund
COPY platform/tsconfig.base.json platform/tsconfig.json ./
COPY platform/packages packages
COPY platform/apps/api apps/api
COPY platform/apps/worker apps/worker
COPY platform/apps/music-worker apps/music-worker
COPY platform/apps/engine-agent apps/engine-agent
COPY platform/apps/ai-map-worker apps/ai-map-worker
# Node strips types at run time; tests and the web sources are not needed.
RUN rm -rf packages/*/test apps/*/test && \
    find packages apps -name '*.test.ts' -delete

# ---------------------------------------------------------------- runtime images

# platform-api (default command), platform-worker (`node apps/worker/src/main.ts`)
# and the operator CLI (`platform …`).
FROM ${NODE_IMAGE} AS platform
RUN groupadd --gid 10001 glob2 && useradd --uid 10001 --gid 10001 --no-create-home --home-dir /nonexistent glob2 && \
    mkdir -p /var/lib/glob2/blobs /var/lib/glob2/keys /var/lib/glob2/relay /var/lib/glob2/engine-agent \
      /var/lib/glob2/db/migrator /var/lib/glob2/db/api /var/lib/glob2/db/worker /etc/glob2 && \
    chown -R 10001:10001 /var/lib/glob2 && \
    chmod 700 /var/lib/glob2/keys /var/lib/glob2/relay /var/lib/glob2/engine-agent /var/lib/glob2/db/*
COPY --from=platform-runtime-deps /app/platform /app/platform
COPY deploy/platform-init.sh /usr/local/bin/glob2-platform-init
RUN printf '#!/bin/sh\nexec node /app/platform/apps/api/src/cli.ts "$@"\n' > /usr/local/bin/platform && \
    chmod 755 /usr/local/bin/platform /usr/local/bin/glob2-platform-init
ENV NODE_ENV=production \
    HTTP_HOST=0.0.0.0 \
    HTTP_PORT=8080 \
    BLOB_STORE=fs \
    BLOB_DIR=/var/lib/glob2/blobs \
    JWT_KEYS_DIR=/var/lib/glob2/keys
WORKDIR /app/platform
USER 10001:10001
EXPOSE 8080
CMD ["node", "apps/api/src/main.ts"]

FROM ${UBUNTU_IMAGE} AS relay
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libssl3t64 wget \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 glob2 && useradd --uid 10001 --gid 10001 --no-create-home --home-dir /nonexistent glob2 \
    && mkdir -p /var/lib/glob2/relay /var/spool/glob2-relay \
    && chown 10001:10001 /var/lib/glob2/relay /var/spool/glob2-relay && chmod 700 /var/lib/glob2/relay
COPY --from=relay-build /output/glob2-relay /usr/local/bin/
COPY deploy/relay-entrypoint.sh /usr/local/bin/glob2-relay-entrypoint
ENV GLOB2_RELAY_PORT=7495 \
    GLOB2_RELAY_SPOOL_DIR=/var/spool/glob2-relay
USER 10001:10001
EXPOSE 7495
ENTRYPOINT ["/usr/local/bin/glob2-relay-entrypoint"]
CMD ["glob2-relay"]

# One image per sim version. Node comes from the official image's binary, so
# glob2 runs on the glibc it was built against.
FROM ${NODE_IMAGE} AS node-binary
FROM ${UBUNTU_IMAGE} AS engine-agent
ARG GLOB2_SIM_VERSION=
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    libfreetype6 libopusfile0 libopus0 libogg0 libspeex1 libfribidi0 libpcre3 libepoxy0 \
    libgl1 libglu1-mesa libssl3t64 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 glob2 && useradd --uid 10001 --gid 10001 --no-create-home --home-dir /nonexistent glob2 \
    && mkdir -p /var/lib/glob2/engine-agent && chown 10001:10001 /var/lib/glob2/engine-agent \
    && chmod 700 /var/lib/glob2/engine-agent
COPY --from=node-binary /usr/local/bin/node /usr/local/bin/node
COPY --from=platform-runtime-deps /app/platform /app/platform
COPY --from=engine-build /output/bin/glob2 /opt/glob2/bin/glob2
COPY --from=engine-build /output/lib/glob2 /opt/glob2/lib/glob2
COPY --from=engine-build /output/licenses /usr/local/share/licenses
COPY --from=engine-build /output/share /opt/glob2/share
COPY --from=engine-build /output/sim-version /output/sim-data-hash /opt/glob2/
COPY deploy/engine-agent-entrypoint.sh /usr/local/bin/glob2-engine-agent-entrypoint
LABEL org.glob2.sim-version="${GLOB2_SIM_VERSION}"
ENV NODE_ENV=production \
    ENGINE_BINARY=/opt/glob2/bin/glob2 \
    ENGINE_WORKDIR=/opt/glob2/share \
    ENGINE_SCRATCH_DIR=/tmp
WORKDIR /app/platform
USER 10001:10001
ENTRYPOINT ["/usr/local/bin/glob2-engine-agent-entrypoint"]
CMD ["node", "apps/engine-agent/src/main.ts"]

# Optional authoring service. Sources/descriptions and binary come from the same engine context.
FROM engine-agent AS ai-map-worker
USER root
RUN apt-get update && apt-get install -y --no-install-recommends python3 python3-pil \
    && rm -rf /var/lib/apt/lists/*
COPY --from=engine-source /src/map/generator/generators /opt/glob2/share/src/map/generator/generators
COPY --from=engine-source /.agents/skills/glob2-map-design/references /opt/glob2/share/.agents/skills/glob2-map-design/references
ENV GLOB2_SOURCE_DIR=/opt/glob2/share
USER 10001:10001
CMD ["node", "apps/ai-map-worker/src/main.ts"]

FROM ${CADDY_IMAGE} AS caddy
RUN setcap -r /usr/bin/caddy && chown -R 10001:10001 /data /config
USER 10001:10001
COPY deploy/Caddyfile /etc/caddy/Caddyfile
COPY --from=web-build /app/platform/apps/web/dist /srv/web
# The WebAssembly client is mounted at /srv/play (see docs/hosting/README.md).

# Community music conversion runs independently of simulation-version agents.
FROM platform AS music-worker
USER root
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg python3 python3-numpy python3-scipy python3-soundfile python3-pip python3-venv && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv --system-site-packages /opt/music && /opt/music/bin/pip install --no-cache-dir pyloudnorm==0.1.1
COPY tools/encode_music.py /app/tools/encode_music.py
COPY tools/music/glob2music /app/tools/music/glob2music
ENV MUSIC_PYTHON=/opt/music/bin/python
USER 10001:10001
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s CMD node -e "const t=Number(require('fs').readFileSync('/tmp/glob2-music-worker-health','utf8'));process.exit(Date.now()-t<90000?0:1)"
CMD ["node", "apps/music-worker/src/main.ts"]
