// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace Hive::Browser
{
// Starts one isolated browser worker; a later result is owned by the caller (free).
void begin(const char *request);
char *result();
}
