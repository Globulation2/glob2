// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's global container pointer, defined once for every test binary. Engine
// tests own an instance through glob2test::HeadlessGlobals; unit tests never set it.
class GlobalContainer;
GlobalContainer* globalContainer = nullptr;
