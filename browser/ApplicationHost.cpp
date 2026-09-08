// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <GraphicContext.h>
#include <emscripten.h>
#include <stdexcept>

namespace GAGCore::ApplicationHost
{
namespace
{
struct ScheduledLoop { std::unique_ptr<Loop> loop; std::function<void()> complete; };
void scheduledFrame(void* opaque)
{
    auto* state = static_cast<ScheduledLoop*>(opaque);
    if (EM_ASM_INT({ return Module.gpuRestorePending ? 1 : 0; })) {
        // SDK-pinned compatibility state owns shaders and streaming buffers.
        // Recreate it before asking the shared renderer to restore textures.
        EM_ASM({
            // Objects from the lost context are already destroyed by WebGL.
            // Forget their handles rather than deleting them in the new context.
            GL.textures.fill(null);
            GL.buffers.fill(null);
            GLImmediate.currentRenderer = null;
            GLImmediate.lastRenderer = null;
            GLImmediate.lastArrayBuffer = null;
            GLImmediate.lastProgram = null;
            GLImmediate.fixedFunctionProgram = null;
            GLImmediate.currentMatrix = 0;
            GLImmediate.totalEnabledClientAttributes = 0;
            GLImmediate.enabledClientAttributes = new Array(2).fill(0);
            GLImmediate.init();
            GLctx.useProgram(null);
            GLctx.currentProgram = 0;
            GLctx.bindBuffer(GLctx.ARRAY_BUFFER, null);
            GLctx.currentArrayBufferBinding = 0;
        });
        GraphicContext::restoreBrowserContext();
        EM_ASM({
            Module.gpuRestorePending = false;
            Module.gpuLost = false;
            Module.visibilityPending = true;
            Module.gpuRestores = (Module.gpuRestores || 0) + 1;
        });
    }
    std::vector<SDL_Event> events;
    SDL_Event event;
    while (SDL_PollEvent(&event)) events.push_back(event);
    if (!state->loop->frame(SDL_GetTicks(), events)) {
        state->loop.reset();
        auto complete = std::move(state->complete);
        delete state;
        complete();
        return;
    }
    // Queue only after the frame returns. During the migration an Asyncify
    // suspension inside a legacy dialog must not start a second frame.
    emscripten_async_call(scheduledFrame, state, state->loop->delay(SDL_GetTicks()));
}
}
void run(std::unique_ptr<Loop> loop, std::function<void()> complete)
{
    auto* state = new ScheduledLoop{std::move(loop), std::move(complete)};
    emscripten_async_call(scheduledFrame, state, 0);
}

void wait(std::uint32_t milliseconds)
{
    emscripten_sleep(milliseconds ? milliseconds : 1);
}
bool takeVisibilityChange(bool& hidden)
{
    const int state = EM_ASM_INT({
        if (!Module.visibilityPending) return -1;
        Module.visibilityPending = false;
        return document.hidden || Module.gpuLost ? 1 : 0;
    });
    if (state < 0) return false;
    hidden = state != 0;
    return true;
}
bool takeViewportSize(int& width, int& height)
{
    return EM_ASM_INT({
        const size = Module.pendingViewport;
        Module.pendingViewport = null;
        if (!size || size.width <= 0 || size.height <= 0) return 0;
        HEAP32[$0 >> 2] = size.width;
        HEAP32[$1 >> 2] = size.height;
        return 1;
    }, &width, &height);
}
namespace {
class BrowserFileSelection : public FileSelection {
    int id;
public:
    explicit BrowserFileSelection(const std::string& extension) {
        id = EM_ASM_INT({
            Module.fileSelections ||= new Map();
            const id = Module.nextFileSelectionId = (Module.nextFileSelectionId || 0) + 1;
            const selection = new Glob2FileSelection([UTF8ToString($0)]);
            Module.fileSelections.set(id, selection);
            selection.pick(document);
            return id;
        }, extension.c_str());
    }
    ~BrowserFileSelection() override {
        EM_ASM({ Module.fileSelections.get($0).dispose(); Module.fileSelections.delete($0); }, id);
    }
    FileSelectionState state() const override {
        return static_cast<FileSelectionState>(EM_ASM_INT({
            const state = Module.fileSelections.get($0).state;
            return state === 'selected' ? 1 : state === 'cancelled' ? 2 : state === 'failed' ? 3 : 0;
        }, id));
    }
    SelectedFile takeFile() override {
        if (state() != FileSelectionState::Selected) throw std::logic_error("No selected file is available");
        const int nameSize = EM_ASM_INT({ return lengthBytesUTF8(Module.fileSelections.get($0).file.name) + 1; }, id);
        const int size = EM_ASM_INT({ return Module.fileSelections.get($0).file.bytes.length; }, id);
        std::vector<char> name(nameSize);
        SelectedFile file;
        file.bytes.resize(size);
        EM_ASM({
            const selection = Module.fileSelections.get($0);
            stringToUTF8(selection.file.name, $1, $2);
            HEAPU8.set(selection.file.bytes, $3);
            selection.file = null;
            selection.state = 'cancelled';
        }, id, name.data(), nameSize, file.bytes.data());
        file.name = name.data();
        return file;
    }
};
}
bool canImportFiles() { return true; }
std::unique_ptr<FileSelection> selectFile(const std::string& extension) {
    return std::make_unique<BrowserFileSelection>(extension);
}
bool storageRestoreFailed() { return EM_ASM_INT({ return Module.storageRestore === 'failed'; }); }
bool canExportFiles() { return true; }
bool exportFile(const std::string& name, const std::vector<unsigned char>& bytes)
{
    return EM_ASM_INT({
        try {
            const blob = new Blob([HEAPU8.slice($1, $1 + $2)], {type:'application/octet-stream'});
            const url = URL.createObjectURL(blob);
            const anchor = document.createElement('a');
            anchor.href = url;
            anchor.download = UTF8ToString($0).replace(/[\\/]/g, '_');
            anchor.click();
            setTimeout(() => URL.revokeObjectURL(url), 60000);
            return 1;
        } catch (_) { return 0; }
    }, name.c_str(), bytes.data(), bytes.size());
}
namespace {
class BrowserPersistence : public Persistence {
    int id;
public:
    BrowserPersistence() {
        id = EM_ASM_INT({
            Module.persistenceResults ||= new Map();
            const id = Module.nextPersistenceId = (Module.nextPersistenceId || 0) + 1;
            Module.persistenceResults.set(id, 0);
            const complete = state => { if (Module.persistenceResults.has(id)) Module.persistenceResults.set(id, state); };
            Module.storage.flush().then(() => complete(1), () => complete(2));
            return id;
        });
    }
    ~BrowserPersistence() override { EM_ASM({ Module.persistenceResults.delete($0); }, id); }
    PersistenceState state() const override {
        return static_cast<PersistenceState>(EM_ASM_INT({ return Module.persistenceResults.get($0); }, id));
    }
};
}
std::unique_ptr<Persistence> persistStorage() { return std::make_unique<BrowserPersistence>(); }
void screenChanged(const char* name)
{
    EM_ASM({ Module['glob2Screen'] = UTF8ToString($0); }, name);
}
void simulationAdvanced(std::uint32_t tick)
{
    EM_ASM({ Module['glob2Tick'] = $0; Module['glob2Screen'] = 'match'; }, tick);
}
void exited(int result)
{
    EM_ASM({
        Module['glob2Screen'] = 'exited';
        if (Module['onGameExit']) Module['onGameExit']($0);
    }, result);
}
void matchFrame(bool paused)
{
    EM_ASM({
        Module['glob2Frames'] = (Module['glob2Frames'] || 0) + 1;
        Module['glob2Paused'] = Boolean($0);
    }, paused);
}
}
