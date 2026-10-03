# Historical AI records

These binary and text records were generated using the pre-fix serializers at
revision `2895a7f3523747e8186047b090884ef026c100c2`, with the current engine fixture
support linked for a 32-by-32 grass map and one local player. They contain actual
format-127 AI layouts, followed by `sentinel = 0x1234abcd`; they are not current
records with their version changed.

Each AI timer is 77. Cabino's active module is its first module and its queue is
empty (the old writer damages nonempty queues). Nicowar uses the default strategy.
Cortex uses its ordinary policy. No new continuation fields are present.

`LegacyAIStateTest.cpp` reads both encodings and verifies historical defaults and
sentinel alignment. The temporary generator and original-source build logs stay
under ignored `artifacts/ai-continuation/pre-fix/` as review evidence.
