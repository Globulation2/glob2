// SPDX-License-Identifier: GPL-3.0-or-later
// Online probes run without game data. SimVersion::local() uses its no-data
// fallback when no file manager is installed. Supply the libgag symbols that
// SimVersion references without linking the game UI into these small probes.
#include <FileManager.h>
#include <Toolkit.h>

namespace GAGCore
{
FileManager *Toolkit::fileManager = nullptr;
StreamBackend *FileManager::openInputStreamBackend(const std::string)
{
	return nullptr;
}
} // namespace GAGCore
