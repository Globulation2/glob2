Tested ed8350bb73150272482ac03b985ee356a7304ca7
Base 710edcbd57e2b819775cfab3c1b36ca3605c64f1
Fetched master ee3be8ecd9d7100de78ef63869271371d427cb66
Linux x86_64 Python 3.14.4
Command: python3 test/build_system/test_scene_boundary.py
Two tests pass. Original two failures reproduced. Five independent Game.h injection negative controls rejected in ResourceRegistry.h, ResourceProperties.h, Material.h, ExperimentalFeatures.h, Types.h; restored after each.
Only Python audit changes; no native builds needed or performed. No simulation/save/replay changes.
