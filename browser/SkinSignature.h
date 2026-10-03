// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <emscripten.h>

// Include in one translation unit per browser executable.
// Asynchronous WebCrypto verification used by colony appearance assertions.
EM_JS(int, startSkinSignature, (const char *key, const char *message, const char *signature), {
    const state = Module.glob2SkinSignatures ||= {next:1, entries:new Map()};
    const id=state.next++; state.entries.set(id,0);
    const decode=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')),c=>c.charCodeAt(0));
    const k=decode(UTF8ToString(key)), s=decode(UTF8ToString(signature)), m=new TextEncoder().encode(UTF8ToString(message));
    try { crypto.subtle.importKey('raw',k,{name:'Ed25519'},false,['verify'])
      .then(k=>crypto.subtle.verify('Ed25519',k,s,m))
      .then(ok=>{if(state.entries.has(id))state.entries.set(id,ok?1:-1);})
      .catch(()=>{if(state.entries.has(id))state.entries.set(id,-1);});
    } catch (_) { state.entries.set(id,-1); }
    return id;
});
EM_JS(int, pollSkinSignature, (int id), { return Module.glob2SkinSignatures?.entries.get(id) ?? -1; });
EM_JS(void, deleteSkinSignature, (int id), { Module.glob2SkinSignatures?.entries.delete(id); });
