# syntax=docker/dockerfile:1.7
#
# Moves the C++ stages' build caches (deploy/Dockerfile's glob2-ccache-<arch> and
# glob2-dev-<arch> cache mounts: the compiler cache and the development store with
# the runtime-asset export and dependency prefixes) in and out of a BuildKit
# builder, so deploy-online.yml can keep them between CI runs as an image in its
# cache repository:
#
#   docker buildx build -f deploy/ccache.Dockerfile --target import \
#       --build-arg SEED=<cache image> deploy/      # before the image builds
#   docker buildx build -f deploy/ccache.Dockerfile --target export \
#       --build-arg STAMP=<unique> --push -t <cache image> deploy/   # after them
#
# STAMP makes the pack step run again; --no-cache would not do, because BuildKit then
# also ignores cache mounts and would pack empty directories. A cache image holds
# /cache.tar (ccache/ and glob2-dev/); images from before the development store
# was kept hold /ccache.tar with the compiler cache alone. A self-hosted builder
# keeps its cache mounts on its own and needs neither.
ARG UBUNTU_IMAGE=ubuntu:24.04
ARG SEED=scratch

FROM ${SEED} AS seed

FROM ${UBUNTU_IMAGE} AS import
ARG TARGETARCH
RUN --mount=type=cache,id=glob2-ccache-${TARGETARCH},target=/cache/ccache,sharing=locked \
    --mount=type=cache,id=glob2-dev-${TARGETARCH},target=/cache/glob2-dev,sharing=locked \
    --mount=type=bind,from=seed,target=/seed \
    if [ -f /seed/cache.tar ]; then tar -xf /seed/cache.tar -C /cache; \
    elif [ -f /seed/ccache.tar ]; then tar -xf /seed/ccache.tar -C /cache/ccache; fi && \
    du -sh /cache/ccache /cache/glob2-dev

FROM ${UBUNTU_IMAGE} AS pack
ARG TARGETARCH
ARG STAMP
RUN --mount=type=cache,id=glob2-ccache-${TARGETARCH},target=/cache/ccache,sharing=locked \
    --mount=type=cache,id=glob2-dev-${TARGETARCH},target=/cache/glob2-dev,sharing=locked \
    echo "pack $STAMP" && \
    # One runtime-asset export per content key accumulates; keep the newest three.
    if [ -d /cache/glob2-dev/caches/runtime-assets ]; then \
        cd /cache/glob2-dev/caches/runtime-assets && \
        ls -1dt -- */ 2>/dev/null | tail -n +4 | xargs -r rm -rf -- && cd /; \
    fi && \
    du -sh /cache/ccache /cache/glob2-dev && \
    tar -cf /cache.tar -C /cache ccache glob2-dev && ls -l /cache.tar

FROM scratch AS export
COPY --from=pack /cache.tar /cache.tar
