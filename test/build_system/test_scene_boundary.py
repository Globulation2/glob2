"""Drawing reads the shared immutable world snapshot, never live simulation state.

See "Scene renderer" in docs/development/reference.md. Render passes are still
Game member functions, so this checks what drawing code reads rather than what
it includes; the Scene types themselves must stay plain presentation data.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

# Scene headers may include each other, the client channel types and plain data
# definitions, but no simulation object headers.
TERRAIN_HEADERS = (
    'TerrainType.h', 'TerrainPresentation.h', 'TerrainProperties.h', 'CellRules.h', 'TerrainRegistry.h',
    'TerrainTypeTable.h', 'TerrainGroup.h', 'TerrainPropertiesLayout.h',
)

FIELD_HEADERS = ('TerrainMovementCosts.h', 'PreparedTerrainCosts.h', 'GradientCosts.h', 'TerrainHazardCost.h', 'GradientBucket.h', 'GradientConstants.h')

SCENE_INCLUDES = {
    'Scene.h', 'SceneEntities.h', 'SceneMap.h', 'ScenePanels.h',
    'sim/presentation/PresentationRequest.h', 'sim/snapshot/WorldSnapshot.h',
    'sim/snapshot/Requirements.h',
    'sim/ClientRequests.h', 'sim/EntityRef.h', 'BitArray.h', 'Ressource.h', 'UnitConsts.h',
    'SDLGraphicContext.h', 'SDL_stdinc.h', 'SDL3/SDL_stdinc.h', 'AITelemetryValue.h',
    *TERRAIN_HEADERS, *FIELD_HEADERS,
    *(f'field/{name}' for name in FIELD_HEADERS), 'map/TerrainProperties.h',
    'ResourceRegistry.h', 'ResourceProperties.h', 'Material.h', 'resource/Material.h', 'BuildingType.h',
    'ExperimentalFeatures.h', 'Types.h', 'SDL3_net/SDL_net.h',
}

DRAWING = ('src/render/GameRender*.cpp', 'src/render/Minimap.cpp', 'src/render/torus/TorusViewRender.cpp', 'src/hud/draw/GameGUIDraw*.cpp',
           'src/hud/touch/GameGUITouchView.cpp', 'src/hud/touch/GameGUITouchPalette.cpp',
           'src/hud/touch/GameGUITouchLens.cpp', 'src/hud/touch/GameGUITouchPlacement.cpp',
           'src/map/editor/MapEditInspector.cpp', 'src/map/editor/EditorDockObjects.cpp',
           'src/map/editor/EditorDock.cpp', 'src/map/editor/PhoneEditorTray.cpp', 'src/hud/GameGUIGhostBuildingManager.cpp',
           'src/hud/GameGUITorus.cpp', 'src/hud/draw/GameGUIParticles.cpp', 'src/hud/GameGUIStep.cpp',
           'src/render/overlay/MarkManager.cpp', 'src/hud/GameGUIDialog.cpp', 'src/hud/input/GameGUIInputMouse.cpp', 'src/hud/input/GameGUIInputMenu.cpp', 'src/hud/GameGUIScript.cpp', 'src/hud/GameGUIToolManager.cpp')

# Live entity tables and queues the simulation mutates.
LIVE_READS = re.compile(
    r'\bteams\[[^\]]*\]\s*->'
    r'|\bmy(?:Units|Buildings)\s*\['
    r'|->getEvent\s*\('
    r'|\bgame\.(?:map|players|teams|buildingsTypes)\b')


class SceneBoundaryTests(unittest.TestCase):
    def test_scene_types_do_not_include_simulation_headers(self):
        include = re.compile(r'#\s*include\s*[<"]([^>"]+)[>"]')
        headers = sorted((ROOT / 'src/render/scene').glob('*.h')) + [ROOT / 'src/ai/telemetry/AITelemetryValue.h']
        # Terrain tables are immutable value definitions, not live simulation
        # objects. Audit their dependencies too so this exemption stays narrow.
        headers += [ROOT / 'src/map' / name for name in
                    TERRAIN_HEADERS]
        headers += [ROOT / 'src/field' / name for name in FIELD_HEADERS]
        # Frozen resource snapshots contain value definitions and experiment keys.
        # Audit their full local dependency chain, not only the Scene include.
        headers += [ROOT / 'src/resource' / name for name in
                    ('ResourceRegistry.h', 'ResourceProperties.h', 'Material.h')]
        headers += [ROOT / 'src/game/ExperimentalFeatures.h', ROOT / 'libgag/include/Types.h']
        for path in headers:
            for name in include.findall(path.read_text()):
                if '/' not in name and name.islower():
                    continue  # standard library
                with self.subTest(header=path.name, include=name):
                    self.assertIn(name, SCENE_INCLUDES | {'SceneBuffer.h'})

    def test_drawing_does_not_read_live_entities(self):
        for pattern in DRAWING:
            for path in sorted(ROOT.glob(pattern)):
                for number, line in enumerate(path.read_text().splitlines(), 1):
                    with self.subTest(file=str(path.relative_to(ROOT)), line=number):
                        self.assertIsNone(LIVE_READS.search(line), line.strip())

    def test_preparation_cannot_capture_or_read_live_objects(self):
        for path in (ROOT / 'src').rglob('*'):
            if path.suffix not in ('.h', '.cpp'):
                continue
            with self.subTest(file=str(path.relative_to(ROOT))):
                self.assertNotRegex(path.read_text(), r'\b(?:extractScene|captureSceneInputs|extractInputPanels)\s*\(')
        for name in ('SceneExtract.cpp', 'SceneExtract.h', 'SceneMap.cpp', 'SceneMap.h'):
            source = (ROOT / 'src/render/scene' / name).read_text()
            with self.subTest(file=name):
                self.assertNotRegex(source, r'#\s*include\s*[<"](?:Game|Map|Unit|Building)\.h[>"]')
                self.assertNotRegex(source, r'\b(?:captureBoundary|invalidateBoundary|extractInputPanels)\s*\(')
                self.assertNotRegex(source, r'\b(?:Game|Map|Unit|Building)\s*(?:const\s*)?[&*]')
        for name in ('SceneEntities.h', 'ScenePanels.h', 'SceneMap.h'):
            source = (ROOT / 'src/render/scene' / name).read_text()
            with self.subTest(file=name):
                self.assertNotRegex(source, r'std::vector\s*<\s*(?:SnapshotUnit|SnapshotBuilding|SceneUnit|SceneBuilding)\s*>')


if __name__ == '__main__':
    unittest.main()
