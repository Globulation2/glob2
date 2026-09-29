# macOS build dependency

Native secure WebSocket support is enabled by default and uses OpenSSL from
Homebrew or MacPorts. The existing application-bundle step copies `libssl`,
`libcrypto`, and their non-system dependencies into `Contents/Frameworks` and
rewrites their install names. Build with `wss=0` when producing a TCP-only
client without OpenSSL or Boost.Beast.
