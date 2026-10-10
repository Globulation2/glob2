# Local Flatpak candidate validation

Candidate: `v0.11.0.3`, public source `aaa73b56543e7a5070f51379e955ec493e22d082`. This is validation of our own Flatpak package, not a Flathub submission or a release qualification. The existing AI-assisted recipe cannot be submitted to Flathub under its current policy.

Environment: Ubuntu 24.04.5 LTS x86_64, host Flatpak 1.14.6, bundled flatpak-builder 1.4.9, GNOME SDK/Platform 51. Exact runtime commits and compiler versions are in the toolchain files. All dependencies are pinned by the candidate's `flatpak/org.globulation2.Globulation2.yml.in`. No dependency source or version changed.

## Build and lint

Rendered the candidate's existing template with the public commit above. Manifest lint and AppStream lint passed (exit 0); AppStream emitted one pedantic notice.

Initial command: `flatpak run --user --command=flathub-build org.flatpak.Builder --install org.globulation2.Globulation2.yml`. The libwebp download succeeded with the manifest's SHA-256 `e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`, resolving the hosted job's earlier HTTP 403 locally.

The original build was deliberately stopped during C++ compilation because its fixed two-job setting made validation slow. It exited 1 from that interruption. `resume.py` reconstructed the logged sandbox command, preserved source, release flags and environment, changed only `scons -j2` to `scons -j8`, and ran the cleanup/export/install phases afterward. Sandbox-local recording dependencies rebuilt on restart. The final resumed build/export/install exited 0. The exact resumed command and both build logs are retained here.

`flatpak run --user --command=flatpak-builder-lint org.flatpak.Builder repo repo` passed (exit 0). Installed app commit: `c434aca4360078a87e8bb3bac3a407e8b520fce72c687e7b940d650511ee9ed8`, version 0.11.0.3.

## Runtime checks

`smoke.sh` contains the exact installed-app commands, all executed with the package's default permissions. It exited 0: version/sim-version, River seed 713 map/preview generation, Castor versus Cortex with game seed 713 for 2048 ticks, replay writing, final save, and continuation to tick 2112.

An additional uninterrupted match used the same map, seed, players and two compute threads, with `--ticks 2112 --telemetry checksums`. All 2048 initial and 64 resumed per-tick checksums matched its trace. Both completed result files have final checksum `1616351272`. Runtime artifacts include map, preview, save, replay, result files and checksum traces.

Graphical startup command: `timeout --signal=TERM 15s env DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus flatpak run --user org.globulation2.Globulation2`. Exit 124 means the process stayed running until timeout; it does not certify visual correctness or gameplay review. The logs report first-profile preferences missing and the SDL3_ttf kerning workaround.

## Limits

No public release, store upload, or Flathub PR was created. This covers one Linux x86_64 sandbox and one generated AI match. It does not qualify historical-save compatibility, replay playback, live multiplayer, cross-platform determinism, other architectures, recording output, or human gameplay/visual review. Flathub eligibility requires a human-authored manifest and human submission under its current requirements.
