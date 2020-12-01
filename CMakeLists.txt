cmake_minimum_required(VERSION 3.13)
project(GLOB2)


if(CMAKE_BUILD_TYPE MATCHES DEBUG)
    # Tell GCC to add macros to the debug information
    add_definitions(-g3)
endif(CMAKE_BUILD_TYPE MATCHES DEBUG)
set(CMAKE_CXX_FLAGS "-Wall ${CMAKE_CXX_FLAGS}")

set(FONT "sans.ttf" CACHE PATH "Path to default font")
set(SERVER_ONLY OFF CACHE BOOL "Only build YOG server")

if (WIN32)
    set(USE_WIN32)
elseif(APPLE)
    set(USE_OSX)
endif()

set(VERSION 0.9.5.0)
set(DATADIR ${CMAKE_SOURCE_DIR})
set(SOURCEDIR ${CMAKE_SOURCE_DIR})
# TODO Don't hard set these
set(HAVE_OPENGL ON)
set(HAVE_FRIBIDI ON)

configure_file(config.h.in config.h)

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

add_subdirectory(libgag)
include_directories(libgag/include)

add_subdirectory(libusl)
include_directories(libusl/src)

set(GLOB2_SOURCES
    src/AINames.cpp
    src/BasePlayer.cpp
    src/BaseTeam.cpp
    src/BitArray.cpp
    src/BuildingUtils.cpp
    src/Bullet.cpp
    src/EntityType.cpp
    src/GameHeader.cpp
    src/Glob2.cpp
    src/GlobalContainer.cpp
    src/LANGameInformation.cpp
    src/LogFileManager.cpp
    src/Map.cpp
    src/MapHeader.cpp
    src/MapThumbnail.cpp
    src/NetBroadcaster.cpp
    src/NetConnection.cpp
    src/NetConnectionThread.cpp
    src/NetConnectionThreadMessage.cpp
    src/NetGamePlayerManager.cpp
    src/NetListener.cpp
    src/NetMessage.cpp
    src/NetReteamingInformation.cpp
    src/NetTestSuite.cpp
    src/Order.cpp
    src/Race.cpp
    src/Sector.cpp
    src/Settings.cpp
    src/UnitType.cpp
    src/UnitUtils.cpp
    src/Utilities.cpp
    src/WinningConditions.cpp
    src/YOGAfterJoinGameInformation.cpp
    src/YOGConsts.cpp
    src/YOGDownloadableMapInfo.cpp
    src/YOGGameInfo.cpp
    src/YOGGameResults.cpp
    src/YOGMessage.cpp
    src/YOGPlayerSessionInfo.cpp
    src/YOGPlayerStoredInfo.cpp
    src/YOGServer.cpp
    src/YOGServerAdministrator.cpp
    src/YOGServerAdministratorCommands.cpp
    src/YOGServerAdministratorList.cpp
    src/YOGServerBannedIPListManager.cpp
    src/YOGServerChatChannel.cpp
    src/YOGServerChatChannelManager.cpp
    src/YOGServerFileDistributationManager.cpp
    src/YOGServerFileDistributor.cpp
    src/YOGServerGame.cpp
    src/YOGServerGameLog.cpp
    src/YOGServerGameRouter.cpp
    src/YOGServerMapDatabank.cpp
    src/YOGServerPasswordRegistry.cpp
    src/YOGServerPlayer.cpp
    src/YOGServerPlayerScoreCalculator.cpp
    src/YOGServerPlayerStoredInfoManager.cpp
    src/YOGServerRouter.cpp
    src/YOGServerRouterAdministrator.cpp
    src/YOGServerRouterAdministratorCommands.cpp
    src/YOGServerRouterManager.cpp
    src/YOGServerRouterPlayer.cpp
    )

if(SERVER_ONLY)
else()
set(GLOB2_SOURCES
    ${GLOB2_SOURCES}
    src/AI.cpp
    src/AICastor.cpp
    src/AIDescriptionScreen.cpp
    src/AIEcho.cpp
    src/AINicowar.cpp
    src/AINull.cpp
    src/AINumbi.cpp
    src/AIToubib.cpp
    src/AIWarrush.cpp
    src/Brush.cpp
    src/Building.cpp
    src/BuildingType.cpp
    src/BuildingsTypes.cpp
    src/CPUStatisticsManager.cpp
    src/Campaign.cpp
    src/CampaignEditor.cpp
    src/CampaignMainMenu.cpp
    src/CampaignMenuScreen.cpp
    src/CampaignSelectorScreen.cpp
    src/ChooseMapScreen.cpp
    src/CreditScreen.cpp
    src/CustomGameOtherOptions.cpp
    src/CustomGameScreen.cpp
    src/DynamicClouds.cpp
    src/EditorMainMenu.cpp
    src/EndGameScreen.cpp
    src/Engine.cpp
    src/Fatal.cpp
    src/FertilityCalculatorDialog.cpp
    src/FertilityCalculatorThread.cpp
    src/FertilityCalculatorThreadMessage.cpp
    src/GUIGlob2FileList.cpp
    src/GUIMapPreview.cpp
    src/Game.cpp
    src/GameEvent.cpp
    src/GameGUI.cpp
    src/GameGUIDefaultAssignManager.cpp
    src/GameGUIDialog.cpp
    src/GameGUIGhostBuildingManager.cpp
    src/GameGUIKeyActions.cpp
    src/GameGUILoadSave.cpp
    src/GameGUIMessageManager.cpp
    src/GameGUIToolManager.cpp
    src/GameHints.cpp
    src/GameObjectives.cpp
    src/GameUtilities.cpp
    src/Glob2Screen.cpp
    src/Glob2Style.cpp
    src/Gradient.cpp
    src/HeightMapGenerator.cpp
    src/IRC.cpp
    src/IRCTextMessageHandler.cpp
    src/IRCThread.cpp
    src/IRCThreadMessage.cpp
    src/IntBuildingType.cpp
    src/KeyboardManager.cpp
    src/LANFindScreen.cpp
    src/LANMenuScreen.cpp
    src/MainMenuScreen.cpp
    src/MapEdit.cpp
    src/MapEditDialog.cpp
    src/MapEditKeyActions.cpp
    src/MapGenerationDescriptor.cpp
    src/MapGenerator.cpp
    src/MapScript.cpp
    src/MapScriptError.cpp
    src/MapScriptUSL.cpp
    src/MarkManager.cpp
    src/Minimap.cpp
    src/MultiplayerGame.cpp
    src/MultiplayerGameEvent.cpp
    src/MultiplayerGameEventListener.cpp
    src/MultiplayerGameScreen.cpp
    src/NetBroadcastListener.cpp
    src/NetEngine.cpp
    src/NewMapScreen.cpp
    src/OverlayAreas.cpp
    src/PerlinNoise.cpp
    src/Player.cpp
    src/ReplayReader.cpp
    src/ReplayWriter.cpp
    src/Ressource.cpp
    src/RessourcesTypes.cpp
    src/SGSL.cpp
    src/ScriptEditorScreen.cpp
    src/SettingsScreen.cpp
    src/SimplexNoise.cpp
    src/SoundMixer.cpp
    src/Team.cpp
    src/TeamStat.cpp
    src/Unit.cpp
    src/UnitConsts.cpp
    src/UnitEditorScreen.cpp
    src/UnitSkin.cpp
    src/UnitsSkins.cpp
    src/VoiceRecorder.cpp
    src/YOGClient.cpp
    src/YOGClientBlockedList.cpp
    src/YOGClientChatChannel.cpp
    src/YOGClientChatListener.cpp
    src/YOGClientCommandManager.cpp
    src/YOGClientCommands.cpp
    src/YOGClientDownloadableMapList.cpp
    src/YOGClientDownloadableMapListener.cpp
    src/YOGClientDownloadingMapScreen.cpp
    src/YOGClientEvent.cpp
    src/YOGClientEventListener.cpp
    src/YOGClientFileAssembler.cpp
    src/YOGClientGameConnectionDialog.cpp
    src/YOGClientGameListListener.cpp
    src/YOGClientGameListManager.cpp
    src/YOGClientLobbyScreen.cpp
    src/YOGClientMapDownloadScreen.cpp
    src/YOGClientMapDownloader.cpp
    src/YOGClientMapUploadScreen.cpp
    src/YOGClientMapUploader.cpp
    src/YOGClientOptionsScreen.cpp
    src/YOGClientPlayerListListener.cpp
    src/YOGClientPlayerListManager.cpp
    src/YOGClientRatedMapList.cpp
    src/YOGClientRouterAdministrator.cpp
    src/YOGLoginScreen.cpp
    src/YOGPlayerPrivateInfo.cpp
    src/YOGRegisterScreen.cpp
    )
endif()

add_executable(glob2 ${GLOB2_SOURCES})

target_link_libraries(glob2 
    gag
    usl
    SDL2 
    SDL2_net 
    SDL2_ttf
    SDL2_image 
    speex
    vorbisfile
    ogg
    z
    fribidi
    pthread
    boost_thread 
    boost_system
    )
