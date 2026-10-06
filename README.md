# WebSocket backpressure test repair verification

Tested 17e5da938fc5ee688fa0106e98a143d50aacea58, base 410ce0ff82d14c94463d2840d2591da0bf373210.

Original Windows failure: https://github.com/Globulation2/glob2/actions/runs/37396129432/job/112056743877
The connection remained healthy, but the 20-second shared deadline expired with zero of 10,000 messages received. The test counts 500 sleeps of 1ms before resuming reads; actual sleeps can exceed requested time. Coarse scheduling is an inference from code/failure shape, not a measured Windows timer quantum.

Local controlled delayed-wakeup reproduction changes only the original fixture's sleep to 40ms: exits 1 after 22.32s with 0 == 10000; no transport changes. Final committed fixture passes 20/20 independent executions, retaining deadline, 10,000 messages, byte/order assertions, and healthy connections. It now also requires every message to be sent before pausing.

Ubuntu 26.04.1 x86_64, GCC 15.2.0, C++20 -O2 -pthread, SDL 3.4.16 / SDL_net 3.2.0 and OpenSSL 3.5.5. All transport support sources freshly compiled; exact compile/link commands, provenance, logs and JUnit in evidence.tar.gz. Test commands in run-commands.txt. This is focused native transport validation, not full engine/platform verification. No production, simulation, save, replay or network protocol code changes. Windows hosted verification is requested with ci:run + ci:windows and remains pending; PR stays draft.
