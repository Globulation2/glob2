"""CppUnit suite sources, relative to test/, shared by host and Android builds."""
CPPUNIT_SOURCES = """
TestsRunner.cpp
../libgag/src/PerformanceTelemetry.cpp

PerlinNoiseTest.cpp
../src/map/generator/shared/Noise.cpp

HelloWorldTest.cpp

BitArrayTest.cpp

BrushAccumulatorTest.cpp
BrushTestStubs.cpp
../src/Brush.cpp

GradientBFSTest.cpp
../src/ai/echo/GradientBFS.cpp

MapQueryTest.cpp
GradientTest.cpp
FetchApportionmentTest.cpp

FertilityFieldTest.cpp
../src/map/FertilityField.cpp
MapQueryTestStubs.cpp
../src/map/Map.cpp
../src/map/gradient/MapGradientField.cpp
../src/map/MapQuery.cpp
../src/map/MapTerrain.cpp
../src/BitArray.cpp
../src/Utilities.cpp
../src/building/BuildingUtils.cpp
../src/unit/UnitUtils.cpp

GameMusicControllerTest.cpp
../src/gui/GameMusicController.cpp

PlayerVoiceDrainTest.cpp
../src/PlayerVoice.cpp

GhostBuildingOverlapTest.cpp

BrushToolHitTest.cpp

TurretScanTileTest.cpp

ParticleCrossfadeTest.cpp

UnitTimingTest.cpp

UnitDrawGeometryTest.cpp

CortexUpgradeTest.cpp

FilenameStripTest.cpp

AllyTeamWidgetIndexTest.cpp

EditorWidgetLayoutTest.cpp

GUIListSelectionTest.cpp

ScrollWheelTargetTest.cpp

BuildingFailureDisplayTest.cpp

OverlayFillTest.cpp
../src/OverlayFill.cpp

MessageRecipientsTest.cpp
../src/net/message/MessageRecipients.cpp

SpriteCenteringTest.cpp

PanelButtonHitTest.cpp

KeyActionLookupTest.cpp
../src/gui/GameGUIKeyActions.cpp
../src/map/edit/MapEditKeyActions.cpp

natsort/NatSortTest.cpp
""".split()
