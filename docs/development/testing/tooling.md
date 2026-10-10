# Tooling verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Python tests

`test/test_*.py` are `unittest` files; those that need a build take the binary
path on the command line (`test_map_cli.py`, `test_map_report.py`). The `check_*.py`
scripts compare full-game traces against retained fixtures and are documented with
their harness in the [verification hub](README.md). `test/build_system/`, `test/deployment/`,
`test/transport/`, `test/relay_service/` and `test/online_service/` test the build
system and the services.
