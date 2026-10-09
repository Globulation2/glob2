# macOS build dependency

The native file manager locates bundled runtime assets through SDL's absolute
Resources directory. Command-line map tools preserve the caller's working
directory for input and output paths; they can run outside the source checkout.

Native secure WebSocket support is enabled by default and uses OpenSSL from
Homebrew or MacPorts. The existing application-bundle step copies `libssl`,
`libcrypto`, and their non-system dependencies into `Contents/Frameworks` and
rewrites their install names. Build with `wss=0` when producing a TCP-only
client without OpenSSL or Boost.Beast.

The release bundle includes campaigns, maps, data, scripts, the project license
and third-party asset attribution. Its Info.plist version is set from the SCons
package version before the bundle is signed.

Release `bundle` and `package` builds require CMake and Homebrew codec dependencies
for the pinned PNG/JPEG/WebP SDL_image build. Assets are exported to smaller verified
encodings, dylib aliases retain one canonical copy, and the release executable is
stripped after its matching dSYM is retained under the build's `symbols/` directory.
Keep that directory with release evidence; it is not included in the app or DMG.
See [release asset and bundle sizes](../docs/development/reference.md#release-asset-and-bundle-sizes).
