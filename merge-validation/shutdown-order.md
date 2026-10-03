# SDL_net shutdown ordering

Fix source: 7be426cf1 (parent f4777296b). Compiler: g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0.
Hosted failure: https://github.com/Globulation2/glob2/actions/runs/37125951012/job/111213035980

The game previously destroyed GlobalContainer, whose graphics context calls
SDL_Quit, before an atexit NET_Quit stopped the resolver threads. The retained
minimal probe reproduces sanitizer failures in all five old-order runs and
passes all five NET_Quit-before-SDL_Quit runs. No sanitizer suppression is used
by the probe. It is compiled with `-g -fsanitize=thread`, linked to the pinned
local SDL3/SDL3_net SDK and run with SDL_VIDEODRIVER=dummy and
TSAN_OPTIONS='halt_on_error=1 exitcode=66'. An argument selects corrected order.

The full game was rebuilt with release=0, CXXFLAGS='-g -fsanitize=thread' and
LINKFLAGS='-fsanitize=thread'. The 600-tick headless threaded game passed. The
300-tick windowed case initially found an independent lock-order inversion in
the host's libdbus during desktop-session startup/shutdown. Its complete log is
retained. Repeating in Xvfb with nonexistent DBUS_SESSION_BUS_ADDRESS and
DBUS_SYSTEM_BUS_ADDRESS socket paths passed; this isolates the desktop bus
from the game test. No sanitizer check was disabled for this retry. The
existing CI report_thread_leaks=0 setting was retained for the windowed case.
The old SDL_DestroyMutex race suppression was removed from test/tsan.supp.

Release native, serial/threaded WebAssembly and packaging builds passed. Three
browser shutdown/save-persistence cases passed, and 258 build contracts passed
with three existing environment skips. Final hosted TSan remains required.
