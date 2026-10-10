# Build and run

Run commands from the repository root. Choose parallelism for available memory and competing builds; CPU count alone is not a safe job limit.

## Native prerequisites

The recipes below follow the native CI dependency environments. Run commands from the repository root after installing the platform’s package manager. The pinned SDL3 helper verifies source hashes and applies the repository’s portability patches; do not replace its prefix with an unrelated SDL2 installation.

### macOS

Install Apple command-line tools and the Homebrew dependencies:

```sh
xcode-select --install
brew install python cmake freetype libpng jpeg-turbo scons pkgconf ccache nasm opusfile opus libogg speex openssl@3 boost fribidi libepoxy
python3 scons/sdl3_dependencies.py --prefix build/sdl3/prefix --work build/sdl3/sources --jobs 2
export GLOB2_SDL3_PREFIX="$PWD/build/sdl3/prefix"
export PATH="$GLOB2_SDL3_PREFIX/bin:$PATH"
export DYLD_LIBRARY_PATH="$GLOB2_SDL3_PREFIX/lib"
```

Homebrew’s OpenSSL is keg-only. Add its include/library paths to the build command:

```sh
python3 tools/dev_build.py --build=build/local-dev \
  CXXFLAGS="-I$(brew --prefix openssl@3)/include" \
  LINKFLAGS="-L$(brew --prefix openssl@3)/lib"
```

### Ubuntu Linux

Ubuntu 24.04 supplies the primary GCC 13 environment. Other Linux distributions need equivalent packages. Display tests additionally use Xvfb and a window manager.

```sh
sudo apt-get update
sudo apt-get install -y git g++-13 python3 python3-venv scons pkg-config ccache cmake nasm libva-dev \
  libfreetype-dev libpng-dev libjpeg-dev libx11-dev libxext-dev libxcursor-dev libxi-dev \
  libxrandr-dev libxss-dev libxtst-dev libasound2-dev libpulse-dev libwayland-dev libxkbcommon-dev \
  libopusfile-dev libopus-dev libogg-dev libspeex-dev libboost-dev libssl-dev zlib1g-dev \
  libfribidi-dev libpcre3-dev libgl1-mesa-dev libglu1-mesa-dev libepoxy-dev \
  libgl1-mesa-dri xvfb xauth openbox x11-utils
python3 scons/sdl3_dependencies.py --prefix build/sdl3/prefix --work build/sdl3/sources --jobs 2
export GLOB2_SDL3_PREFIX="$PWD/build/sdl3/prefix"
export PATH="$GLOB2_SDL3_PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$GLOB2_SDL3_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 tools/dev_build.py CXX=g++-13 --build=build/local-dev
```

### Windows

Use the MSYS2 **MINGW64** shell, matching the native Windows workflow. Keep the repository in a path without spaces for upstream dependency tools.

```sh
pacman -S --needed git make mingw-w64-x86_64-toolchain mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-python mingw-w64-x86_64-scons mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-freetype mingw-w64-x86_64-opusfile mingw-w64-x86_64-opus \
  mingw-w64-x86_64-libogg mingw-w64-x86_64-speex mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-fribidi mingw-w64-x86_64-pcre mingw-w64-x86_64-openssl \
  mingw-w64-x86_64-boost mingw-w64-x86_64-libepoxy mingw-w64-x86_64-libsystre \
  mingw-w64-x86_64-nasm mingw-w64-x86_64-ccache
python3 scons/sdl3_dependencies.py --prefix build/sdl3/prefix --work build/sdl3/sources --jobs 2
export GLOB2_SDL3_PREFIX="$PWD/build/sdl3/prefix"
export PATH="$GLOB2_SDL3_PREFIX/bin:$PATH"
python3 tools/dev_build.py mingw=1 --build=build/local-dev
```

Keep the prefix’s DLL directory in PATH when running the game or tests from this shell. Packaged Windows builds stage runtime DLLs beside their binaries; see [package size](package-size.md).

The declarations are [vcpkg.json](../../vcpkg.json), [SConstruct](../../SConstruct), and the [SDL3 source lock](../../scons/sdl3-versions.json). Recording codecs build from pinned sources; see [media dependencies](media-dependencies.md).

## Routine editing

```sh
python3 tools/dev_build.py --build=build/local-dev tests
python3 test/run_tests.py --build-dir build/local-dev --quick
./build/local-dev/src/glob2
```

Keep the platform-specific compiler/OpenSSL options from setup on subsequent invocations. On Windows run `./build/local-dev/src/glob2.exe`. The explicit output directory above makes the runner and executable location unambiguous.

The helper enables compiler caching, limits default concurrency and selects a separate fast output directory. It forwards SCons options and targets. Use [build options](build-options.md) for full symbols, cache settings, PCH/unity experiments and browser/mobile variants. Run the client in the output directory reported by the build; outputs are isolated by toolchain, role and configuration.

## Ordinary and optimized builds

```sh
scons
scons release=1
scons release=1 tests
scons role=relay release=1 relay
```

Ordinary debug compilation provides detailed debugger symbols. Use `release=1` for representative performance and release packaging. Fast development success does not establish release readiness. [Native tests](../../test/README.md) explains test selection; [release guides](../releases/README.md) explain packaging and publication.

## Output and configuration

Default outputs live under `build/<toolchain>/<role>/<mode>`; native toolchains are `darwin`, `linux` and `mingw`. `--build=PATH` selects an output directory with a matching build identity. Generated configuration is under that directory, never in the source root. Options are explicit on each invocation, not loaded from an earlier options cache. `scons -c` cleans the selected configuration. `GLOB2_BUILD_DIR` tells test tools about a nondefault directory.

`mingw=1` selects a native Windows build; `mingwcross=1` selects cross-compilation. The retired lobby `server` and `router` roles are rejected; use the [match relay](../multiplayer/relay.md) and [online platform](../multiplayer/architecture.md).

## Platform guides

- [Browser build and testing](../../browser/README.md).
- [Android and iOS development](../mobile/development.md).
- [Shared SDK storage](development-storage.md).

## Troubleshooting

A missing dependency should be resolved in the selected platform environment, not by copying generated headers from another target. Check output identity and prefix overrides before investigating stale binaries. Keep harness runs in disposable profiles. For Windows compilation avoid identifiers `near`, `far` and `small`, which system headers define as macros. For cache conflicts, [inspect the managed store](development-storage.md) before pruning; builds never prune it automatically.
