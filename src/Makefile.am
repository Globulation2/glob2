bin_PROGRAMS = glob2

if LINKSTATIC
glob2_static_lib =\
	 /usr/lib/libSDL_image.a\
	 /usr/lib/libSDL_net.a\
	 /usr/lib/libSDL_ttf.a\
	 /usr/lib/libpng*.a\
	 /usr/lib/libtiff.a\
	 /usr/lib/libjpeg.a\
	 /usr/lib/libz.a\
	 /usr/lib/libvorbisfile.a\
	 /usr/lib/libvorbis.a\
	 /usr/lib/libogg.a\
	 /usr/lib/libspeex.a\
	 /usr/lib/libSDL.a\
	 /usr/lib/libaudio.a\
	 /usr/lib/libaa.a\
	 /usr/lib/libncurses.a\
	 /usr/lib/libslang.a\
	 /usr/lib/libgpm.a\
	 /usr/lib/libfreetype.a
else
glob2_static_lib =
endif

glob2_SOURCES=\
AI.cpp \
AI.h \
AICastor.cpp \
AICastor.h \
AIEcho.cpp \
AIEcho.h \
AIImplementation.h \
AINicowar.cpp \
AINicowar.h \
AINull.cpp \
AINull.h \
AINumbi.cpp \
AINumbi.h \
AIToubib.cpp \
AIToubib.h \
AIWarrush.cpp \
AIWarrush.h \
BitArray.cpp \
BitArray.h \
Brush.cpp \
Brush.h \
Building.cpp \
Building.h \
BuildingType.cpp \
BuildingType.h \
BuildingsTypes.cpp \
BuildingsTypes.h \
Bullet.cpp \
Bullet.h \
Campaign.cpp \
Campaign.h \
CampaignEditor.cpp \
CampaignEditor.h \
CampaignMenuScreen.cpp \
CampaignMenuScreen.h \
CampaignScreen.cpp \
CampaignScreen.h \
CampaignSelectorScreen.cpp \
CampaignSelectorScreen.h \
ChooseMapScreen.cpp \
ChooseMapScreen.h \
ConfigFiles.h \
CPUStatisticsManager.cpp \
CPUStatisticsManager.h \
CreditScreen.cpp \
CreditScreen.h \
CustomGameScreen.cpp \
CustomGameScreen.h \
DynamicClouds.cpp \
DynamicClouds.h \
EndGameScreen.cpp \
EndGameScreen.h \
Engine.cpp \
Engine.h \
EntitiesTypes.h \
EntityType.cpp \
EntityType.h \
Fatal.cpp \
Fatal.h \
GUIGlob2FileList.cpp \
GUIGlob2FileList.h \
GUIMapPreview.cpp \
GUIMapPreview.h \
Game.cpp \
Game.h \
GameEvent.cpp \
GameEvent.h \
GameGUI.cpp \
GameGUI.h \
GameGUIDialog.cpp \
GameGUIDialog.h \
GameGUILoadSave.cpp \
GameGUILoadSave.h \
GameHeader.cpp \
GameHeader.h \
GameUtilities.cpp \
GameUtilities.h \
Glob2.cpp \
Glob2.h \
Glob2Screen.cpp \
Glob2Screen.h \
Glob2Style.cpp \
Glob2Style.h \
GlobalContainer.cpp \
GlobalContainer.h \
Header.h \
HeightMapGenerator.cpp \
HeightMapGenerator.h \
IRC.cpp \
IRC.h \
LANGameInformation.cpp \
LANGameInformation.h \
IntBuildingType.cpp \
IntBuildingType.h \
LogFileManager.cpp \
LogFileManager.h \
MainMenuScreen.cpp \
MainMenuScreen.h \
Map.cpp \
Map.h \
MapAssembler.cpp \
MapAssembler.h \
MapEdit.cpp \
MapEdit.h \
MapGenerationDescriptor.cpp \
MapGenerationDescriptor.h \
MapGenerator.cpp \
MapHeader.cpp \
MapHeader.h \
Marshaling.h \
MultiplayerGameScreen.cpp \
MultiplayerGameScreen.h \
MultiplayerGame.cpp \
MultiplayerGame.h \
MultiplayerGameEvent.cpp \
MultiplayerGameEvent.h \
MultiplayerGameEventListener.cpp \
MultiplayerGameEventListener.h \
NetBroadcaster.cpp \
NetBroadcaster.h \
NetBroadcastListener.cpp \
NetBroadcastListener.h \
NetConnection.cpp \
NetConnection.h \
NetConsts.h \
NetDefine.h \
NetEngine.cpp \
NetEngine.h \
NetListener.cpp \
NetListener.h \
NetMessage.cpp \
NetMessage.h \
NetTestSuite.cpp \
NetTestSuite.h \
NetTextMessageHandler.cpp \
NetTextMessageHandler.h \
NewMapScreen.cpp \
NewMapScreen.h \
NonANSICStdWrapper.h \
Order.cpp \
Order.h \
PerlinNoise.cpp \
PerlinNoise.h \
Player.cpp \
Player.h \
Race.cpp \
Race.h \
Ressource.cpp \
Ressource.h \
RessourceType.h \
RessourcesTypes.cpp \
RessourcesTypes.h \
SGSL.cpp \
SGSL.h \
ScriptEditorScreen.cpp \
ScriptEditorScreen.h \
Sector.cpp \
Sector.h \
Settings.cpp \
Settings.h \
SettingsScreen.cpp \
SettingsScreen.h \
SoundMixer.cpp \
SoundMixer.h \
Team.cpp \
Team.h \
TeamStat.cpp \
TeamStat.h \
TerrainType.h \
Unit.cpp \
Unit.h \
UnitConsts.cpp \
UnitConsts.h \
UnitEditorScreen.cpp \
UnitEditorScreen.h \
UnitSkin.cpp \
UnitSkin.h \
UnitType.cpp \
UnitType.h \
UnitsSkins.cpp \
UnitsSkins.h \
Utilities.cpp \
Utilities.h \
Version.h \
VoiceRecorder.cpp \
VoiceRecorder.h \
YOGClient.cpp \
YOGClient.h \
YOGConsts.h \
YOGEvent.cpp \
YOGEvent.h \
YOGEventListener.cpp \
YOGEventListener.h \
YOGGame.cpp \
YOGGame.h \
YOGGameInfo.cpp \
YOGGameInfo.h \
YOGGameServer.cpp \
YOGGameServer.h \
YOGLoginScreen.cpp \
YOGLoginScreen.h \
YOGMapDistributor.cpp \
YOGMapDistributor.h \
YOGMessage.cpp \
YOGMessage.h \
YOGPasswordRegistry.cpp \
YOGPasswordRegistry.h \
YOGPlayer.cpp \
YOGPlayer.h \
YOGPlayerInfo.cpp \
YOGPlayerInfo.h \
YOGScreen.cpp \
YOGScreen.h


if LINKSTATIC
glob2_LDADD = ../libgag/src/libgag.a -lGL -lGLU -lpthread $(glob2_static_lib)
else
glob2_LDADD = ../libgag/src/libgag.a @LIBS@ @SDL_LIBS@ @VORBISFILE_LIBS@ @VORBIS_LIBS@ -lSDL_ttf $(glob2_static_lib)
endif
INCLUDES = @SDL_CFLAGS@ @VORBIS_CFLAGS@ -I@srcdir@/../libgag/include
