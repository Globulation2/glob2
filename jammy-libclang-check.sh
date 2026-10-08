set -euxo pipefail
apt-get update -qq
apt-get install -y --no-install-recommends ca-certificates curl g++
# Ubuntu 22.04 needs LLVM's signed repository for the pinned major.
. /etc/os-release
if [ "$VERSION_CODENAME" = jammy ]; then
  curl --fail --silent --show-error --retry 3 https://apt.llvm.org/llvm-snapshot.gpg.key | tee /usr/share/keyrings/llvm.asc >/dev/null
  echo "deb [signed-by=/usr/share/keyrings/llvm.asc] https://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" | tee /etc/apt/sources.list.d/llvm18.list >/dev/null
  apt-get update -qq
fi
apt-get install -y --no-install-recommends libclang-18-dev python3-venv nodejs npm
python3 -m venv artifacts/map-generator-tools-venv
artifacts/map-generator-tools-venv/bin/python -m pip install clang==18.1.8
artifacts/map-generator-tools-venv/bin/python test/test_toolkit_instrumentation.py
