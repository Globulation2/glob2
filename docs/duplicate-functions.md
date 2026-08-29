# Duplicate Functions in C++ Codebase

Analysis of function names and bodies across `glob2/src/` using universal-ctags and body hashing.
Generated 2026-04-09.

## Summary

- 3,899 total function definitions scanned
- 37 groups of functions with identical bodies across different files
- Key consolidation opportunities for the Rust port noted below

---

## Identical Function Bodies Across Files

### GameHints / GameObjectives (near-identical classes)

These two classes have the same structure with only naming differences:

| GameHints method | GameObjectives method | Files |
|------------------|-----------------------|-------|
| `getNumberOfHints` (line 30) | `getNumberOfObjectives` (line 38) | GameHints.cpp, GameObjectives.cpp |
| `setHintHidden` (line 71) | `setObjectiveHidden` (line 87) | GameHints.cpp, GameObjectives.cpp |
| `setHintVisible` (line 79) | `setObjectiveVisible` (line 95) | GameHints.cpp, GameObjectives.cpp |
| `isHintVisible` (line 87) | `isObjectiveVisible` (line 103) | GameHints.cpp, GameObjectives.cpp |
| `getScriptNumber` (line 105) | `getScriptNumber` (line 196) | GameHints.cpp, GameObjectives.cpp |

**Rust port note:** Unify into a single generic type (e.g., `ScriptEntry { hidden, script_number }`) with a kind enum or type alias.

### YOG Listener Pattern (5-6 classes)

Identical `addListener`/`removeListener` implementations across:

- YOGClient.cpp (`addEventListener` / `removeEventListener`)
- YOGClientChatChannel.cpp
- YOGClientDownloadableMapList.cpp
- YOGClientGameListManager.cpp
- YOGClientPlayerListManager.cpp
- IRCTextMessageHandler.cpp (`addTextMessageListener`)

**Rust port note:** Not needed in Rust -- use channels or a simple callback vec utility.

### KeyActions Classes

`getName` and `getAction` are identical in:
- GameGUIKeyActions.cpp:163, :168
- MapEditKeyActions.cpp:95, :100

**Rust port note:** Unify into a single KeyAction type.

### ServerPlayer Variants

`isConnected` and `sendMessage`/`sendNetMessage` identical in:
- YOGServerPlayer.cpp:329, :336
- YOGServerRouterPlayer.cpp:102, :42

### Map/Game Info Getters

`setNumberOfTeams` / `getNumberOfTeams` duplicated between:
- NetGamePlayerManager.cpp / MapHeader.cpp / YOGGameInfo.cpp

`setCheckSum` duplicated between:
- ReplayReader.cpp:211 / ReplayWriter.cpp:99

### Dialog onAction

Identical `onAction` in:
- GameGUIDialog.cpp:57 / MapEditDialog.cpp:46
- CreditScreen.cpp:195 / MainMenuScreen.cpp:76

### Upload/Download Progress

`getPercentUploaded` identical in:
- YOGClientMapDownloader.cpp:90
- YOGClientMapUploader.cpp:127

---

## High-Count Duplicate Function Names (non-virtual, non-operator)

These function names appear in multiple files. Many are simple getters returning a member, but worth reviewing for consolidation:

| Count | Function Name |
|-------|---------------|
| 9 | `getPlayerID` |
| 9 | `getFileID` |
| 8 | `getMessage` |
| 6 | `getGameID` |
| 5 | `setMapHeader`, `setMapDiscovered`, `getMapHeader`, `getUsername`, `isConnected` |
| 4 | `updateGuardAreasGradient`, `updateGlobalGradient`, `updateForbiddenGradient`, `updateClearAreasGradient` |
| 4 | `stringIP`, `setValues`, `getValue`, `getRessource`, `getReason`, `getPlayerName` |
| 4 | `getName`, `getMapID`, `getIPAddress`, `getGameHeader`, `getError`, `getChatChannel`, `getAction`, `addPlayer` |

---

## Methodology

1. Extracted all function definitions using `universal-ctags -R --languages=C++ --c++-kinds=f --fields=+nE`
2. For duplicate names: grouped by function name, excluded operators and known virtual/interface methods
3. For identical bodies: extracted source from opening `{` to closing `}`, stripped comments and whitespace, MD5-hashed, grouped by hash, filtered to groups spanning multiple files
4. Trivial functions (< 20 chars normalized body) were excluded from body comparison
