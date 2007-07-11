set(glob2_SOURCES
AICastor.cpp
AI.cpp
AIEcho.cpp
AINicowar.cpp
AINull.cpp
AINumbi.cpp
AIToubib.cpp
AIWarrush.cpp
BitArray.cpp
Brush.cpp
Building.cpp
BuildingsTypes.cpp
BuildingType.cpp
Bullet.cpp
Campaign.cpp
CampaignEditor.cpp
CampaignMenuScreen.cpp
CampaignScreen.cpp
CampaignSelectorScreen.cpp
ChooseMapScreen.cpp
CPUStatisticsManager.cpp
CreditScreen.cpp
CustomGameScreen.cpp
DynamicClouds.cpp
EndGameScreen.cpp
Engine.cpp
EntityType.cpp
Fatal.cpp
Game.cpp
GameEvent.cpp
GameGUI.cpp
GameGUIDialog.cpp
GameGUILoadSave.cpp
GameHeader.cpp
GameUtilities.cpp
Glob2.cpp
Glob2Screen.cpp
Glob2Style.cpp
GlobalContainer.cpp
GUIGlob2FileList.cpp
GUIMapPreview.cpp
HeightMapGenerator.cpp
IntBuildingType.cpp
IRC.cpp
LANFindScreen.cpp
LANGameInformation.cpp
LANMenuScreen.cpp
LogFileManager.cpp
MainMenuScreen.cpp
MapAssembler.cpp
Map.cpp
MapEdit.cpp
MapGenerationDescriptor.cpp
MapGenerator.cpp
MapHeader.cpp
MultiplayerGame.cpp
MultiplayerGameEvent.cpp
MultiplayerGameEventListener.cpp
MultiplayerGameScreen.cpp
NetBroadcaster.cpp
NetBroadcastListener.cpp
NetConnection.cpp
NetEngine.cpp
NetListener.cpp
NetMessage.cpp
NetTestSuite.cpp
NetTextMessageHandler.cpp
NewMapScreen.cpp
Order.cpp
PerlinNoise.cpp
Player.cpp
Race.cpp
Ressource.cpp
RessourcesTypes.cpp
ScriptEditorScreen.cpp
Sector.cpp
Settings.cpp
SettingsScreen.cpp
SGSL.cpp
SoundMixer.cpp
Team.cpp
TeamStat.cpp
UnitConsts.cpp
Unit.cpp
UnitEditorScreen.cpp
UnitSkin.cpp
UnitsSkins.cpp
UnitType.cpp
Utilities.cpp
VoiceRecorder.cpp
YOGClient.cpp
YOGEvent.cpp
YOGEventListener.cpp
YOGGame.cpp
YOGGameInfo.cpp
YOGGameServer.cpp
YOGLoginScreen.cpp
YOGMapDistributor.cpp
YOGMessage.cpp
YOGPasswordRegistry.cpp
YOGPlayer.cpp
YOGPlayerInfo.cpp
YOGScreen.cpp
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

#TODO: remove this from the code
add_definitions(-DHAVE_CONFIG_H -DHAVE_OPENGL -DAUDIO_RECORDER_OSS)

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${libgag_SOURCE_DIR}/include ${SDL_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})
link_directories(${libgag_BINARY_DIR}/src ${SDL_LIBRARY} ${OPENGL_LIBRARIES})

add_executable(glob2 ${glob2_SOURCES})
target_link_libraries (glob2 gag SDL SDL_ttf SDL_image SDL_net GL GLU speex vorbisfile)
