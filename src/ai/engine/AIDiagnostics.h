// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace AIEngine
{
// Produced privately by a controller; the owner publishes at delivery. An empty
// path denotes a standard stream, selected by standardOutput. File headers
// are emitted only when the destination file is empty.
struct DiagnosticRecord { std::string path, header, text; bool standardOutput = false; };
}
