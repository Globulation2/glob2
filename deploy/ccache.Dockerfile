# syntax=docker/dockerfile:1.7
#
# Moves the C++ stages' compiler cache (the glob2-ccache-<arch> cache mount in
# deploy/Dockerfile) in and out of a BuildKit builder, so deploy-online.yml can keep
# it between CI runs as an image in its cache repository:
#
#   docker buildx build -f deploy/ccache.Dockerfile --target import \
#       --build-arg SEED=<cache image> deploy/      # before the image builds
#   docker buildx build -f deploy/ccache.Dockerfile --target export \
#       --build-arg STAMP=<unique> --push -t <cache image> deploy/   # after them
#
# STAMP makes the pack step run again; --no-cache would not do, because BuildKit then
# also ignores cache mounts and would pack an empty directory.
#
# A cache image holds only /ccache.tar. A self-hosted builder keeps its cache
# mount on its own and needs neither.
ARG UBUNTU_IMAGE=ubuntu:24.04
ARG SEED=scratch

FROM ${SEED} AS seed

FROM ${UBUNTU_IMAGE} AS import
ARG TARGETARCH
RUN --mount=type=cache,id=glob2-ccache-${TARGETARCH},target=/ccache,sharing=locked \
    --mount=type=bind,from=seed,target=/seed \
    if [ -f /seed/ccache.tar ]; then tar -xf /seed/ccache.tar -C /ccache; fi && \
    du -sh /ccache

FROM ${UBUNTU_IMAGE} AS pack
ARG TARGETARCH
ARG STAMP
RUN --mount=type=cache,id=glob2-ccache-${TARGETARCH},target=/ccache,sharing=locked \
    echo "pack $STAMP" && (CCACHE_DIR=/ccache ccache --show-stats 2>/dev/null || true) && \
    tar -cf /ccache.tar -C /ccache . && ls -l /ccache.tar

FROM scratch AS export
COPY --from=pack /ccache.tar /ccache.tar
