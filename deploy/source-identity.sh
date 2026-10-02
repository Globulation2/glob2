#!/bin/sh
# Docker build helper. The client build embeds a Git source identity in its
# test binaries (test/build_provenance.py), but .git is not part of the build
# context, so the copied source tree becomes a one-commit repository.
set -eu
cd "${1:-/source}"
git init -q
git add -A
git -c user.name=docker -c user.email=docker@localhost commit -qm 'build context'
