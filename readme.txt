Tested f3c7ddb9e45b12bfda75779b793019d29eddf93d
Base terrain-catalogue 351832745acb18768a6db734a76e15bdc5541d07
Fetched master ee3be8ecd9d7100de78ef63869271371d427cb66
Linux x86_64, Python 3.14.4
Command: python3 test/build_system/test_scene_boundary.py
Before: four failures. After: two tests pass. Negative controls: append #include "Game.h" to each of TerrainTypeTable.h, TerrainGroup.h, TerrainPropertiesLayout.h independently; all rejected. Files restored.
Only Python contract inventory changes. No native build, simulation, save or replay behavior changes; native matrix omitted.
