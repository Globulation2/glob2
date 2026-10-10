# Browser platform

The browser client shares simulation, formats and online play with native clients. Browser-specific hosting, persistence, scheduling and diagnostics stay behind platform interfaces.

## Build and understand

1. [Build, play and test](../../browser/README.md).
2. [Architecture and runtime packaging](implementation.md).
3. [Storage and save completion](storage.md), [viewport and input](viewport.md), [network transports](gateway.md), and [music playback](audio.md).

## Architecture decisions

These accepted decisions explain why current boundaries exist. Use the architecture guide for the current system overview.

- [001: isolated build identities](adr-001-build-isolation.md).
- [002: application host boundary](adr-002-host-migration.md).
- [003: screens and session ownership](adr-003-screen-execution.md).
- [004: cooperative loading](adr-004-cooperative-loading.md).
- [005: generation randomness](adr-005-generation-randomness.md).
- [006: WebGL2 rendering](adr-006-webgl2-rendering.md).
- [007: interpreter lifetimes](adr-007-script-lifetimes.md).

Related: [engine architecture](../architecture/README.md), [verification policy](../development/verification.md), and [browser publication](../releases/README.md).
