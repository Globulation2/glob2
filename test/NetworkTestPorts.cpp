// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "YOGConsts.h"
#include <cstdlib>
#include <cstdio>
#include <cerrno>

namespace {
// This translation unit replaces YOGConsts.cpp only in native network integration fixtures.
// Production executables retain their constant protocol ports.
Uint16 port(unsigned offset) {
    const char* setting = std::getenv("GLOB2_TEST_PORT_BASE");
    if (!setting) {
        return static_cast<Uint16>(7489 + offset);
    }
    char* end = nullptr;
    errno = 0;
    const auto base = std::strtoul(setting, &end, 10);
    if (errno || !*setting || *end || base < 1024 || base > 65533) {
        std::fputs("GLOB2_TEST_PORT_BASE must be between 1024 and 65533\n", stderr);
        std::exit(EXIT_FAILURE);
    }
    return static_cast<Uint16>(base + offset);
}
}
const Uint16 YOG_SERVER_PORT = port(0);
const Uint16 YOG_SERVER_ROUTER_PORT = port(1);
const Uint16 YOG_ROUTER_PORT = port(2);
const std::string YOG_SERVER_IP = "yog.globulation2.org";
//const std::string YOG_SERVER_IP = "127.0.0.1";

const std::string YOG_SERVER_FOLDER = "beta4/";
