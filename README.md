# Simulation 194 fixture repair evidence

For PR #1365; compiled base 7b594e5ec769c52d74c5fb33c2ba1b1555b79409 and fixture revision a6d07caaa0.
This dedicated evidence branch is temporary review material, not maintained product documentation.

Native verification passes with optimized GCC13/Ubuntu24.04 and the exact hosted GCC11/Ubuntu22.04 build.
The archive retains commands, compiler provenance, JUnit, preview reports and complete checksum records
for resources, 1024-tick AI Studio, generated/reloaded 64-tick Generator Studio and Maxima's 512-record
continuation plus 256-record midpoint reload with one/four workers. Input saves and scripts remain
unchanged in the source repository. All 174 browser Node contracts pass.

Browser qualification of all 30 original cases is still running. This is native evidence only,
not a complete local or hosted green checkpoint. Two earlier Chromium graphics failures remain
separate work. Windows/macOS fixture verification remains hosted.

Extract with tar -xzf native-reference-evidence.tar.gz; begin with qualification.json, status.json
and gcc11-status.json. Runtime binaries retain actual base provenance. Full trace selection is
bounded to 512 MiB input and 64 MiB compressed output. The original 256 MiB raw limit rejected
publication before archiving and was explicitly raised to retain every complete native record.
Byte-identical payloads are stored as tar hard links. Every original trace path and byte remains
available after extraction, without storing duplicate GCC11/GCC13 and serial/parallel records.

Archive SHA-256: 5b0ddab57ebb3f039d82e8ac898664031b0a410a23b990b428bd41bb10a63b1d
