Browser startup cadence repair

Final source:5298d624690d8853a0ab37d46b81751b6a4ea352, based on master c688790991f08bc439ef586983758b167c5a1bdc. Both release Wasm game runtimes rebuilt at this exact revision.

Unmodified hosted release d9ec807052 artifact13127946510 (run41403897288) fails the new threaded slow-animation regression at its unchanged60second main-menu limit. Its full original viewport sequence underCPUaffinity0,1 also produces6passes/1high-densitystartupfailure/1existingSoftware-onlyskip. Failure snapshots confirm visible/focused state, allpackagesready, artwork notready; bringingthepageforwarddoesnotresolveit. Read-only instrumented observations show assetpreparationprogressing, not deadlocked.

Timer turns alone (ba3f6421c5) pass the slow-animation regression but still fail the originaltwo-CPUhigh-densitycase; that intermediateunmergedresultisretained. Final5298 additionally limits the static splash to atmost5FPS duringbrowserpreparation and restores the requested renderpacingonhandover/cancellation.

Final focused checks: Software+WebGL2 eachpass serial/threadedslow-animationstartup and realheadfulbackground/resume(6cases). Originalhigh-densityviewportcase passes underCPUaffinity0,1 in ChromiumWebGL2 and WebKitSoftware(2cases). FirefoxSoftwarecolor/handoverpasses; existingFirefoxWebGLuniformcapturestillfails exactlyasbaseline. Total9passed,1pre-existingFirefoxdisplay/capturefailure; noassertionsorjobsdisabled.32browserpackagecontractspass.

These are functionalchecks and constrainedcase observations, not claimsabout CIqueue/execution/runner-minute savings. Nativebaselinec688 has3458JUnitcasesand18CLIchecks green; nativeasset-loop delaystill3ms because allbehaviorchanges are browser-guarded. Simulation/save/replay versioninputs are unchanged. Complete current-master hostedandlocalbrowsermatricesremaininprogress.
