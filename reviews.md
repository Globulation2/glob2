Two independent subagent reviews of production commit 325357ec2:

Architecture review: no remaining blockers. Confirmed typed reservation ownership, completed-tick boundary, borrowed task lifetime, barrier on exception, stable order publication, cache synchronization, direct-step completion and waiting-tick drain. Incorporated requested drains before public Map::syncStep/configureCompute and comments explaining preparation-first serial error handling.

Compatibility review: no blockers. Confirmed VERSION_MINOR139 / replay floor139 / SIM_REVISION21, explicit v137/v138 replay rejection, unchanged durable save floor58 and wire protocol58, unit test-double updates and CI suite inventory. Independently ran test/check_sim_revision.py against origin/master successfully. Required final cross-host validation and linked evidence before merging. Windows/browser/Android remain unverified.

These are automated subagent reviews, not a maintainer playtest. The observation boundary can change routes and AI trajectories; expected pacing/rules remain unchanged, but interactive feel still needs maintainer playtesting.
