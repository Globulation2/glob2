# AI random-stream reference

`numbi-castor-v121.game.gz` is a version-121 initial save with Numbi and
Castor on symmetric-arena generator 15, map seed 42, game seed 19, width and
height 7, and two teams. The compressed checksum sidecar records 2,048 ticks
from that save under the default eight-tick gradient schedule.

`test/check_telemetry_simulation.py` compares the same save and sidecar on
Linux, Windows and local builds. This gives Numbi's AI-owned random stream a
cross-platform execution check as well as checking the older retained saves.
The sidecar was generated from the macOS release build at version 121; gzip
uses an mtime of zero.
