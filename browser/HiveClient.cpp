// SPDX-License-Identifier: GPL-3.0-or-later
#include "hive/HiveBrowser.h"
#include <emscripten.h>
namespace Hive::Browser
{
// clang-format off
void begin(const char *text)
{
	MAIN_THREAD_EM_ASM(
		{
			if (Module.hiveWorker)
				Module.hiveWorker.terminate();
			Module.hiveResult = null;
			Module.hiveWorker = new Worker(new URL('hive-worker.js', document.baseURI));
			Module.hiveWorker.onmessage = e =>
			{
				Module.hiveResult = JSON.stringify(e.data);
				Module.hiveWorker.terminate();
			};
			Module.hiveWorker.onerror = () =>
			{
				Module.hiveResult = '{"ok":false}';
				Module.hiveWorker.terminate();
			};
			Module.hiveWorker.postMessage(UTF8ToString($0));
			clearTimeout(Module.hiveTimeout);
			Module.hiveTimeout = setTimeout(() =>
												 {
													 if (Module.hiveResult === null)
													 {
														 Module.hiveWorker.terminate();
														 Module.hiveResult = '{"ok":false}';
													 }
												 },
											5000);
		},
		text);
}
char *result()
{
	return reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
		if (Module.hiveResult === null || Module.hiveResult === undefined)
			return 0;
		const r = Module.hiveResult;
		Module.hiveResult = null;
		return stringToNewUTF8(r);
	}));
}
// clang-format on
}
