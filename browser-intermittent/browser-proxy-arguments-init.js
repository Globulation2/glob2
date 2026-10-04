(() => {
  let original;
  globalThis.glob2ProxyCalls = 0;
  globalThis.glob2InvalidProxyCalls = [];
  Object.defineProperty(globalThis, '__emscripten_receive_on_main_thread_js', {
    configurable: true,
    get() { return wrapped; },
    set(value) { original = value; }
  });
  function wrapped(funcIndex, emAsmAddr, callingThread, numCallArgs, args) {
    ++globalThis.glob2ProxyCalls;
    const target = emAsmAddr ? ASM_CONSTS[emAsmAddr] : proxiedFunctionTable[funcIndex];
    if (!target || target.length !== numCallArgs / 2 || (funcIndex && emAsmAddr)) {
      glob2InvalidProxyCalls.push({funcIndex, emAsmAddr, callingThread, numCallArgs, args,
        expected: target?.length, target: target?.toString(), stack: new Error().stack,
        memoryBytes: HEAPU8.length, heap: Array.from(HEAPU32.subarray(args >>> 2, (args >>> 2) + 32))});
    }
    return original(funcIndex, emAsmAddr, callingThread, numCallArgs, args);
  }
})();
