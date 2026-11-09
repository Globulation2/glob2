#!/bin/bash
# Kept for existing commands: sweep_shapes.py runs the same sweep in parallel.
#   bash .agents/skills/glob2-map-design/scripts/sweep_shapes.sh glacis [--set key=value ...]
exec python3 "$(dirname "$0")/sweep_shapes.py" "$@"
