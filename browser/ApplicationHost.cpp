// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <EventQueue.h>
#include "Runtime.h"
#include <emscripten/threading.h>
#include <BrowserTextInput.h>
#include <InterfacePresentation.h>
#include <algorithm>
#include <map>
#include <set>
#include <cstdlib>
#include <GraphicContext.h>
#include <emscripten.h>
#include <stdexcept>
#include <algorithm>

namespace GAGCore::ApplicationHost
{
void initializeOpenGLContext()
{
    // SDL3 uses the HTML5 context API, bypassing Browser.createContext and the
    // callbacks that initialize the pinned Emscripten legacy GL emulation.
    EM_ASM({
        Browser.useWebGL = true;
        Module['ctx'] = GLctx;
        Browser.moduleContextCreatedCallbacks.forEach(function(callback) { callback(); });
    });
}
namespace
{
struct Diagnostics {
    std::string screen, screenClass, import;
    std::uint32_t tick = 0;
    unsigned frames = 0;
    int paused = -1, torus = -1, torusSettled = -1, room = -1, custom = -1;
    bool hasTick = false;
    std::map<std::uintptr_t, std::string> controls;
};
thread_local Diagnostics diagnostics;
// DOM-owned state crosses to the application worker once per host turn. Shared
// UI queries below consume this snapshot instead of making separate proxies.
struct HostState {
    bool active = false;
    // Keep this packed-double layout in sync with sampleHostState's JS writes.
    enum Slot { FrameGate, RestorePending, Visibility, Width, Height, Presentation,
                Count = Presentation + 10 };
    double values[Count]{};
};
thread_local HostState hostState;
void publishDiagnostics(bool hostTurn = false) {
    auto &d = diagnostics;
    static const bool checkErrors = std::getenv("GLOB2_WEBGL_ERRORS") != nullptr;
    int dimensions[3]{};
    EM_ASM({
        if (typeof GLctx === 'undefined' || !GLctx || GLctx.isContextLost()) return;
        HEAP32[$0 >> 2] = GLctx.drawingBufferWidth;
        HEAP32[($0 >> 2) + 1] = GLctx.drawingBufferHeight;
        HEAP32[($0 >> 2) + 2] = $1 ? GLctx.getError() : -1;
    }, dimensions, checkErrors);
    // One publication at a frame boundary; strings are consumed synchronously.
    MAIN_THREAD_EM_ASM({
        const screen = UTF8ToString($0); const imported = UTF8ToString($1);
        if (screen) Module.glob2Screen = screen;
        const screenClass = UTF8ToString($9);
        if (screenClass) Module.glob2ScreenClass = screenClass;
        if (imported) Module.importState = imported;
        if ($2) Module.glob2Tick = $3 >>> 0;
        Module.glob2Frames = (Module.glob2Frames || 0) + $4;
        if ($5 >= 0) Module.glob2Paused = !!$5;
        if ($6 >= 0) Module.glob2Torus = !!$6;
        if ($14 >= 0) Module.glob2TorusSettled = !!$14;
        if ($7 >= 0) Module.glob2RoomCanStart = !!$7;
        if ($8 >= 0) Module.glob2CustomGameReady = !!$8;
        if ($10) Module.glob2Loop = (Module.glob2Loop || 0) + 1;
        if ($11) {
            const previous = Module.glob2RenderContext;
            Module.glob2RenderContext = ({width:$11, height:$12,
                error:$13 < 0 ? null : ($13 || previous?.error || 0)});
        }
    }, d.screen.c_str(), d.import.c_str(), d.hasTick, d.tick, d.frames,
       d.paused, d.torus, d.room, d.custom, d.screenClass.c_str(), hostTurn,
       dimensions[0], dimensions[1], dimensions[2], d.torusSettled);
    for (const auto &[owner, json] : d.controls) {
        MAIN_THREAD_EM_ASM({
            Module.glob2Controls ||= new Map();
            const text = UTF8ToString($1);
            if (text) Module.glob2Controls.set($0, JSON.parse(text));
            else Module.glob2Controls.delete($0);
        }, owner, json.c_str());
    }
    d = {};
}
// Capture DOM-owned state once; consumers use it only during this host frame.
void sampleHostState()
{
    // Tests may hold a host turn without assuming its timer's JavaScript realm.
    auto &host = hostState;
    MAIN_THREAD_EM_ASM({
        const offset = $0 >> 3;
        HEAPF64.fill(0, offset, offset + $2);
        HEAPF64[offset] = Module.glob2FrameGate?.() === false ? 1 : 0;
        if (HEAPF64[offset]) return;
        HEAPF64[offset + 1] = +!!Module.gpuRestorePending;
        const lost = $1 || (typeof GLctx !== 'undefined' && GLctx && GLctx.isContextLost());
        if (lost) Module.gpuLost = true;
        const hidden = !!(document.hidden || Module.gpuLost);
        HEAPF64[offset + 2] = Module.visibilityPending || Module.hostHidden !== hidden ? +hidden : -1;
        Module.visibilityPending = false;
        Module.hostHidden = hidden;
        const size = Module.pendingViewport;
        // A hidden application returns before consuming viewport changes.
        // Retain them until its first visible turn.
        if (!hidden && size && size.width > 0 && size.height > 0) {
            HEAPF64[offset + 3] = size.width;
            HEAPF64[offset + 4] = size.height;
            Module.pendingViewport = null;
        }
        const v = Module.presentationMetrics;
        if (v) HEAPF64.set([v.width,v.height,v.safe.left,v.safe.top,v.safe.right,v.safe.bottom,
            v.keyboardInset,+v.touch,+v.pointer,+v.hover], offset + 5);
    }, host.values, EM_ASM_INT({ return typeof GLctx !== 'undefined' && GLctx && GLctx.isContextLost() ? 1 : 0; }), HostState::Count);
}
struct HostFrameScope {
    HostFrameScope() { hostState.active = true; }
    ~HostFrameScope() { hostState.active = false; }
};
struct ScheduledLoop { std::unique_ptr<Loop> loop; std::function<void()> complete; };
void scheduledFrame(void* opaque)
{
    auto* state = static_cast<ScheduledLoop*>(opaque);
    sampleHostState();
    auto &host = hostState;
    if (host.values[HostState::FrameGate]) {
        emscripten_async_call(scheduledFrame, state, 10);
        return;
    }
    EM_ASM({
        if (typeof GLctx === 'undefined' || !GLctx || Module.contextEventsInstalled) return;
        Module.contextEventsInstalled = true;
        GLctx.canvas.addEventListener('webglcontextlost', event => {
            event.preventDefault(); Module.gpuLost = true;
        });
        GLctx.canvas.addEventListener('webglcontextrestored', () => { Module.gpuRestorePending = true; });
    });
    if (EM_ASM_INT({ return Module.gpuRestorePending ? 1 : 0; }) ||
        host.values[HostState::RestorePending]) {
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
        EM_ASM({ Module.gpuRestorePending = false; Module.gpuLost = false; });
        MAIN_THREAD_EM_ASM({
            Module.gpuRestorePending = false;
            Module.gpuLost = false;
            // Restoration does not make a background tab visible. Keep its
            // simulation suspended, and its viewport request pending.
            Module.hostHidden = !!document.hidden;
            Module.visibilityPending = false;
            HEAPF64[$0 >> 3] = +Module.hostHidden;
            Module.gpuRestores = (Module.gpuRestores || 0) + 1;
        }, host.values + HostState::Visibility);
    }
    bool running;
    {
        GAGCore::EventQueue events;
        SDL_Event event;
        while (SDL_PollEvent(&event)) events.push_back(event);
        const HostFrameScope scope;
        running = state->loop->frame(SDL_GetTicks(), events.events());
    }
    publishDiagnostics(true);
    if (!running) {
        state->loop.reset();
        {
            auto complete = std::move(state->complete);
            delete state;
            complete();
        }
        Glob2Browser::releaseApplicationThread();
        return;
    }
    // Each callback completes before the next frame is scheduled. Browser UI
    // transitions and loading jobs must return control to this host.
    const auto delay = state->loop->delay(SDL_GetTicks());
    emscripten_async_call(scheduledFrame, state, delay == AnimationFrameDelay ? -1 : int(delay));
}
}
void run(std::unique_ptr<Loop> loop, std::function<void()> complete)
{
    Glob2Browser::hosted = true;
    auto* state = new ScheduledLoop{std::move(loop), std::move(complete)};
    emscripten_async_call(scheduledFrame, state, 0);
}

void wait(std::uint32_t)
{
    throw std::logic_error("Blocking application loops are unavailable in the browser; use scheduled screens or jobs");
}
bool takeVisibilityChange(bool& hidden)
{
    if (hostState.active) {
        const int state = int(hostState.values[HostState::Visibility]);
        hostState.values[HostState::Visibility] = -1;
        if (state < 0) return false;
        hidden = state != 0;
        return true;
    }
    if (EM_ASM_INT({ return typeof GLctx !== 'undefined' && GLctx && GLctx.isContextLost() ? 1 : 0; }))
        MAIN_THREAD_EM_ASM({ Module.gpuLost = true; });
    const int state = MAIN_THREAD_EM_ASM_INT({
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
    if (hostState.active) std::copy_n(hostState.values + HostState::Presentation, 10, values);
    else {
        MAIN_THREAD_EM_ASM({
            const v=Module.presentationMetrics;
            if (!v) return;
            const values=Array.of(v.width,v.height,v.safe.left,v.safe.top,v.safe.right,v.safe.bottom,
                v.keyboardInset,+v.touch,+v.pointer,+v.hover);
            for (let i=0;i<values.length;++i) HEAPF64[($0>>3)+i]=values[i];
        },values);
    }
    if (values[0]>0 && values[1]>0) {
        metrics.width=values[0];metrics.height=values[1];
        metrics.safe={values[2],values[3],values[4],values[5]};metrics.keyboardInset=values[6];
        input.touch=values[7];input.pointer=values[8];input.hover=values[9];
        return true;
    }
    return false;
}
// window, document and the clipboard belong to the page. In the threaded runtime
// this code runs on the application worker, which has none of them, so both calls
// run on the page's thread, synchronously, while the click that caused them still
// grants transient activation. If a browser refuses the new tab anyway, the page
// offers a link the player taps instead (glob2OpenUrl in shell.html).
bool openUrl(const std::string& url)
{
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
        return false;
    return MAIN_THREAD_EM_ASM_INT({ return Module.glob2OpenUrl(UTF8ToString($0)); }, url.c_str()) != 0;
}
void prepareUrlWindow()
{
    MAIN_THREAD_EM_ASM({ Module.glob2PrepareWindow(); });
}
bool copyText(const std::string& text)
{
    return MAIN_THREAD_EM_ASM_INT({
        const value = UTF8ToString($0);
        Module.glob2LastCopy = value;
        // A textarea and execCommand: older browsers and pages without the
        // Clipboard API (http: origins); it also needs the click's activation.
        const fallback = () => {
            try {
                const area = document.createElement('textarea');
                area.value = value;
                area.setAttribute('readonly', '');
                area.style.cssText = 'position:fixed;left:-9999px;top:0;opacity:0';
                document.body.appendChild(area);
                area.select();
                const copied = document.execCommand('copy');
                area.remove();
                return copied;
            } catch (_) { return false; }
        };
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(value).then(() => { Module.glob2CopyState = 'copied'; },
                () => { Module.glob2CopyState = fallback() ? 'copied' : 'failed'; });
            return 1;
        }
        const copied = fallback();
        Module.glob2CopyState = copied ? 'copied' : 'failed';
        return copied ? 1 : 0;
    }, text.c_str());
}
bool takeViewportSize(int& width, int& height)
{
    if (hostState.active) {
        if (hostState.values[HostState::Width] <= 0 || hostState.values[HostState::Height] <= 0) return false;
        width = int(hostState.values[HostState::Width]); height = int(hostState.values[HostState::Height]);
        hostState.values[HostState::Width] = hostState.values[HostState::Height] = 0;
        return true;
    }
    return MAIN_THREAD_EM_ASM_INT({
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
        id = MAIN_THREAD_EM_ASM_INT({
            Module.fileSelections ||= new Map();
            const id = Module.nextFileSelectionId = (Module.nextFileSelectionId || 0) + 1;
            const selection = new Glob2FileSelection([UTF8ToString($0)]);
            Module.fileSelections.set(id, selection);
            selection.pick(document);
            return id;
        }, extension.c_str());
    }
    ~BrowserFileSelection() override {
        MAIN_THREAD_EM_ASM({ Module.fileSelections.get($0).dispose(); Module.fileSelections.delete($0); }, id);
    }
    FileSelectionState state() const override {
        return static_cast<FileSelectionState>(MAIN_THREAD_EM_ASM_INT({
            const state = Module.fileSelections.get($0).state;
            return state === 'selected' ? 1 : state === 'cancelled' ? 2 : state === 'failed' ? 3 : 0;
        }, id));
    }
    SelectedFile takeFile() override {
        if (state() != FileSelectionState::Selected) throw std::logic_error("No selected file is available");
        const int nameSize = MAIN_THREAD_EM_ASM_INT({ return lengthBytesUTF8(Module.fileSelections.get($0).file.name) + 1; }, id);
        const int size = MAIN_THREAD_EM_ASM_INT({ return Module.fileSelections.get($0).file.bytes.length; }, id);
        std::vector<char> name(nameSize);
        SelectedFile file;
        file.bytes.resize(size);
        MAIN_THREAD_EM_ASM({
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
bool storageRestoreFailed() { return MAIN_THREAD_EM_ASM_INT({ return Module.storageRestore === 'failed'; }); }
// Packages come from browser/asset-loader.js on the page's thread. A build without a package of that
// name (for instance the test harness, which preloads everything) has the data.
bool assetPackageReady(const char* name)
{
    return MAIN_THREAD_EM_ASM_INT({
        const assets = Module.glob2Assets;
        const name = UTF8ToString($0);
        if (!assets || !assets.manifest.packages.some(entry => entry.name === name)) return 1;
        return assets.states[name] === 'ready' ? 1 : 0;
    }, name);
}
void requestAssetPackage(const char* name)
{
    MAIN_THREAD_EM_ASM({
        const assets = Module.glob2Assets;
        const name = UTF8ToString($0);
        if (assets?.manifest.packages.some(entry => entry.name === name)) assets.request(name);
    }, name);
}
std::vector<std::string> takeInstalledAssetPackages()
{
    std::vector<std::string> names;
    char* list = reinterpret_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
        const installed = Module.glob2Assets?.takeInstalled?.() || [];
        if (!installed.length) return 0;
        const text = installed.join('\n');
        const size = lengthBytesUTF8(text) + 1;
        const pointer = _malloc(size);
        stringToUTF8(text, pointer, size);
        return pointer;
    }));
    if (!list) return names;
    std::string text(list);
    std::free(list);
    for (std::size_t start = 0; start <= text.size();) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        if (end > start) names.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return names;
}
bool canExportFiles() { return true; }
bool exportFilePath(const std::string& path)
{
    return MAIN_THREAD_EM_ASM_INT({
        const name=UTF8ToString($0).split(/[\\/]/).pop();
        (async()=>{
            try {
                const root=await (await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');
                const handle=await root.getFileHandle(encodeURIComponent(name));
                const file=await handle.getFile(); const url=URL.createObjectURL(file); const anchor=document.createElement('a');
                anchor.href=url; anchor.download=name; anchor.click();
                setTimeout(()=>URL.revokeObjectURL(url),60000);
            } catch (error) {
                Module.printErr?.('Recording export: '+error);
                if (Module.glob2RecordingFilesUI) Module.glob2RecordingFilesUI.error=String(error.message || error);
            }
        })();
        return 1;
    },path.c_str());
}
bool exportFile(const std::string& name, const std::vector<unsigned char>& bytes)
{
    return MAIN_THREAD_EM_ASM_INT({
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
        id = MAIN_THREAD_EM_ASM_INT({
            Module.persistenceResults ||= new Map();
            const id = Module.nextPersistenceId = (Module.nextPersistenceId || 0) + 1;
            Module.persistenceResults.set(id, 0);
            const complete = state => { if (Module.persistenceResults.has(id)) Module.persistenceResults.set(id, state); };
            Module.storage.flush().then(() => complete(1), () => complete(2));
            return id;
        });
    }
    ~BrowserPersistence() override { MAIN_THREAD_EM_ASM({ Module.persistenceResults.delete($0); }, id); }
    PersistenceState state() const override {
        return static_cast<PersistenceState>(MAIN_THREAD_EM_ASM_INT({ return Module.persistenceResults.get($0); }, id));
    }
};
}
std::unique_ptr<Persistence> persistStorage() { return std::make_unique<BrowserPersistence>(); }
void importChanged(const char* state) { diagnostics.import = state; }
void screenChanged(const char* name) { diagnostics.screen = diagnostics.screenClass = name; }
void simulationAdvanced(std::uint32_t tick) {
    diagnostics.tick = tick; diagnostics.hasTick = true; diagnostics.screen = "match";
}
void exited(int result) {
    diagnostics.screen = "exited";
    publishDiagnostics();
    Glob2Browser::completed(result);
}
void roomReady(bool canStart) { diagnostics.room = canStart; }
void customGameReady(bool canStart) { diagnostics.custom = canStart; }
bool controlsObserved() { return true; }
void controlsChanged(const void *owner, const char *json) {
    diagnostics.controls[reinterpret_cast<std::uintptr_t>(owner)] = json ? json : "";
}
void matchFrame(bool paused) { ++diagnostics.frames; diagnostics.paused = paused; }
void overviewDrawn(bool drawn, bool settled) {
    diagnostics.torus = drawn;
    diagnostics.torusSettled = settled;
}

}

namespace GAGCore {
namespace {
std::map<const void*,BrowserTextChange> browserTextCallbacks;
std::set<const void*> browserTextVisible;
}
void forgetBrowserTextInput(const void* owner) {
    browserTextCallbacks.erase(owner);browserTextVisible.erase(owner);
    MAIN_THREAD_EM_ASM({ Module.textBridge?.remove($0); },owner);
}
void focusBrowserTextInput(const void* owner) {
    MAIN_THREAD_EM_ASM({ Module.textBridge?.focus($0); },owner);
}
void beginBrowserTextFrame() {
    // DOM editing is synchronized before any dialog action can read its model.
    auto callbacks=browserTextCallbacks;
    for (const auto& [owner,changed]:callbacks) {
        size_t cursor=0;int action=0;
        char* value=reinterpret_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
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
    MAIN_THREAD_EM_ASM({ Module.textBridge?.end();Module.textBridge?.begin(); });
    browserTextVisible.clear();
}
void browserTextInput(const void* owner,SDL_Rect rect,int width,int height,const std::string& value,
    bool password,size_t maximum,BrowserTextChange changed,const SDL_Rect* clip,bool selectAll) {
    browserTextVisible.insert(owner);browserTextCallbacks[owner]=std::move(changed);
    const SDL_Rect visible=clip ? *clip : SDL_Rect{0,0,width,height};
    MAIN_THREAD_EM_ASM({ Module.textBridge?.field($0,{x:$1,y:$2,w:$3,h:$4},$5,$6,UTF8ToString($7),!!$8,$9,{x:$10,y:$11,w:$12,h:$13},!!$14); },
        owner,rect.x,rect.y,rect.w,rect.h,width,height,value.c_str(),password,maximum,visible.x,visible.y,visible.w,visible.h,selectAll);
}
bool hasBrowserTextInput(const void* owner) { return browserTextCallbacks.count(owner)>0; }
}

#include "SkinSignature.h"
