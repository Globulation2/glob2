# Windows LAN TLS and CI reporting

Fix source: c5aeccc9f76252d6494c868c5880adecd1ac07ff.
Hosted failure: https://github.com/Globulation2/glob2/actions/runs/37125951012/job/111213035926

The Windows run built successfully and passed 397 engine cases, but its two
LAN/WSS cases failed creating certificate extensions. Unlike Unix enumeration,
Windows resolver enumeration retained scoped IPv6 strings. The retained
OpenSSL probe demonstrates that a SAN inventory with fe80::1%12 is rejected;
the unscoped inventory succeeds. Windows now excludes scoped entries just as
Unix already did. Future extension parse failures include the extension name
and OpenSSL error. Final Windows verification remains required.

The affected two production LAN/WSS tests pass on Linux (27.9s); native game,
engine harness and transport/online probes build. All 258 build contracts pass
with three existing environment skips. The duplicate Windows CI summary/upload
block was removed; the original summary and artifact upload remain.
