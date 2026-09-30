# macOS build dependency

Native secure WebSocket support is enabled by default and uses OpenSSL from
Homebrew or MacPorts. The existing application-bundle step copies `libssl`,
`libcrypto`, and their non-system dependencies into `Contents/Frameworks` and
rewrites their install names. Build with `wss=0` when producing a TCP-only
client without OpenSSL or Boost.Beast.

The release bundle includes campaigns, maps, data, scripts, the project license
and third-party asset attribution. Its Info.plist version is set from the SCons
package version before the bundle is signed.
