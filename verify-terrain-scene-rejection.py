from pathlib import Path
import tempfile, shutil, importlib.util, unittest
spec = importlib.util.spec_from_file_location("boundary", "test/build_system/test_scene_boundary.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
original = m.ROOT
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    sources = list((original / "src/render/scene").glob("*.h"))
    sources += [original / "src/ai/telemetry/AITelemetryValue.h"]
    sources += [original / "src/map" / n for n in ("TerrainType.h", "TerrainPresentation.h", "TerrainProperties.h", "TerrainCompatibility.h")]
    for source in sources:
        dest = root / source.relative_to(original)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
    m.ROOT = root
    with (root / "src/map/TerrainCompatibility.h").open("a") as stream:
        stream.write('\n#include "Game.h"\n')
    case = m.SceneBoundaryTests("test_scene_types_do_not_include_simulation_headers")
    result = unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite([case]))
    assert len(result.failures) == 1 and not result.errors
    assert "Game.h" in result.failures[0][1]
    print("PASS: forbidden simulation include through TerrainCompatibility.h is detected.")
