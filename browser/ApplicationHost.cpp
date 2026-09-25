// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <BrowserTextInput.h>
#include <InterfacePresentation.h>
#include <map>
#include <set>
#include <cstdlib>
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
    // Each callback completes before the next frame is scheduled. Browser UI
    // transitions and loading jobs must return control to this host.
    emscripten_async_call(scheduledFrame, state, state->loop->delay(SDL_GetTicks()));
}
}
void run(std::unique_ptr<Loop> loop, std::function<void()> complete)
{
    auto* state = new ScheduledLoop{std::move(loop), std::move(complete)};
    emscripten_async_call(scheduledFrame, state, 0);
}

void wait(std::uint32_t)
{
    throw std::logic_error("Blocking application loops are unavailable in the browser; use scheduled screens or jobs");
}
bool takeVisibilityChange(bool& hidden)
{
    const int state = EM_ASM_INT({
        // WebGL becomes lost before its DOM event is dispatched. Do not let
        // a scheduled frame query invalid GPU capabilities in that interval.
        if (typeof GLctx !== 'undefined' && GLctx && GLctx.isContextLost())
            Module.gpuLost = true;
        const current = Boolean(document.hidden || Module.gpuLost);
        // Querying the current state as well as the event latch makes the host
        // resilient to a visibility edge delivered between browser callbacks.
        if (!Module.visibilityPending && Module.hostHidden === current) return -1;
        Module.visibilityPending = false;
        Module.hostHidden = current;
        return current ? 1 : 0;
    });
    if (state < 0) return false;
    hidden = state != 0;
    return true;
}
bool presentationMetrics(ViewportMetrics& metrics,InputCapabilities& input)
{
    double values[10]{};
    EM_ASM({
        const v=Module.presentationMetrics;
        if (!v) return;
        const values=Array.of(v.width,v.height,v.safe.left,v.safe.top,v.safe.right,v.safe.bottom,
            v.keyboardInset,+v.touch,+v.pointer,+v.hover);
        for (let i=0;i<values.length;++i) HEAPF64[($0>>3)+i]=values[i];
    },values);
    if (values[0]>0 && values[1]>0) {
        metrics.width=values[0];metrics.height=values[1];
        metrics.safe={values[2],values[3],values[4],values[5]};metrics.keyboardInset=values[6];
        input.touch=values[7];input.pointer=values[8];input.hover=values[9];
        return true;
    }
    return false;
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
void importChanged(const char* state) { EM_ASM({ Module.importState = UTF8ToString($0); }, state); }
void screenChanged(const char* name)
{
    EM_ASM({ Module['glob2Screen'] = Module['glob2ScreenClass'] = UTF8ToString($0); }, name);
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
void roomReady(bool canStart) { EM_ASM({ Module.glob2RoomCanStart = Boolean($0); }, canStart); }
void matchFrame(bool paused)
{
    EM_ASM({
        Module['glob2Frames'] = (Module['glob2Frames'] || 0) + 1;
        Module['glob2Paused'] = Boolean($0);
    }, paused);
}
void overviewDrawn(bool drawn)
{
    // Reported every match frame; publish only changes.
    static int published = -1;
    if (published == int(drawn)) return;
    published = drawn;
    EM_ASM({ Module['glob2Torus'] = Boolean($0); }, drawn);
}
}

namespace GAGCore {
namespace {
std::map<const void*,BrowserTextChange> browserTextCallbacks;
std::set<const void*> browserTextVisible;
}
void forgetBrowserTextInput(const void* owner) {
    browserTextCallbacks.erase(owner);browserTextVisible.erase(owner);
    EM_ASM({ Module.textBridge?.remove($0); },owner);
}
void beginBrowserTextFrame() {
    // DOM editing is synchronized before any dialog action can read its model.
    auto callbacks=browserTextCallbacks;
    for (const auto& [owner,changed]:callbacks) {
        size_t cursor=0;int action=0;
        char* value=reinterpret_cast<char*>(EM_ASM_PTR({
            const change=Module.textBridge?.take($0);
            if (!change) return 0;
            HEAPU32[$1>>2]=lengthBytesUTF8(change.value.slice(0,change.cursor));
            HEAP32[$2>>2]=change.action || 0;
            const size=lengthBytesUTF8(change.value)+1;
            const pointer=_malloc(size);stringToUTF8(change.value,pointer,size);return pointer;
        },owner,&cursor,&action));
        if (value) { std::string text(value);std::free(value);if(browserTextCallbacks.count(owner)) changed(text,cursor,action); }
    }
}
void endBrowserTextFrame() {
    std::erase_if(browserTextCallbacks,[](const auto& item){return !browserTextVisible.count(item.first);});
    EM_ASM({ Module.textBridge?.end();Module.textBridge?.begin(); });
    browserTextVisible.clear();
}
void browserTextInput(const void* owner,SDL_Rect rect,int width,int height,const std::string& value,
    bool password,size_t maximum,BrowserTextChange changed,const SDL_Rect* clip) {
    browserTextVisible.insert(owner);browserTextCallbacks[owner]=std::move(changed);
    const SDL_Rect visible=clip ? *clip : SDL_Rect{0,0,width,height};
    EM_ASM({ Module.textBridge?.field($0,{x:$1,y:$2,w:$3,h:$4},$5,$6,UTF8ToString($7),!!$8,$9,{x:$10,y:$11,w:$12,h:$13}); },
        owner,rect.x,rect.y,rect.w,rect.h,width,height,value.c_str(),password,maximum,visible.x,visible.y,visible.w,visible.h);
}
bool hasBrowserTextInput(const void* owner) { return browserTextCallbacks.count(owner)>0; }
}
