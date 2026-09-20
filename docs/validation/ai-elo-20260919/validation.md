# UI validation

Validated locally on macOS with the final 3,000-game ratings:

```sh
scons -j4 release=1 server=0 custom-setup-test
./build/src/CustomGameSetupHarness
./build/src/CustomGameSetupHarness artifacts/pr364-ui ui
./build/src/CustomGameSetupHarness artifacts/pr364-ui ai-profile
```

All commands exited zero. The retained logs cover [model, ordering, save/load and replay checks](validation-harness.txt), [three interactive launch flows](validation-ui.txt), and the [focused profile capture](validation-profile.txt). The final [ratings screenshot](../../win-probability/ai-strengths.png) was visually inspected; labels and ratings fit.

The optional `ai-profile` argument captures only the AI panel. The broader visual mode hit an unrelated map-preview zoom assertion on macOS before this final capture; that broader mode is not claimed to pass locally. No simulation code or serialized AI IDs change in this UI PR, and no new cross-platform simulation checksum comparison is claimed.
