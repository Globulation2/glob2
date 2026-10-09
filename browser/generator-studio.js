// One frozen generator per document; all source runs in Glob2's interpreter.
(function (root) {
  const bridge =
    root.glob2StudioEnvelope ||
    (typeof require === "function" ? require("./studio-envelope.js") : null);
  const CHANNEL = "glob2-generator-studio";
  function validLaunch(m, run, revision) {
    if (
      !bridge.envelope(m, CHANNEL, run, revision) ||
      m.type !== "launch" ||
      typeof m.source !== "string" ||
      m.source.includes("\0") ||
      new TextEncoder().encode(m.source).length === 0 ||
      new TextEncoder().encode(m.source).length > 262144 ||
      !m.settings ||
      typeof m.settings !== "object" ||
      Array.isArray(m.settings) ||
      !Number.isInteger(m.settings.seed) ||
      m.settings.seed < 0 ||
      m.settings.seed > 4294967295 ||
      m.settings.candidates !== 1 ||
      m.settings.startingUnitLevel !== 0 ||
      !m.settings.params ||
      typeof m.settings.params !== "object" ||
      Array.isArray(m.settings.params)
    )
      return false;
    return (
      Object.keys(m.settings).length === 4 &&
      Object.entries(m.settings.params).length <= 72 &&
      Object.entries(m.settings.params).every(
        ([k, v]) =>
          /^[A-Za-z0-9_-]{1,64}$/.test(k) &&
          Number.isSafeInteger(v) &&
          v >= -2147483648 &&
          v <= 2147483647,
      )
    );
  }
  if (typeof module !== "undefined" && module.exports)
    module.exports = { validLaunch };
  if (
    !root.location ||
    !root.location.pathname.endsWith("/generator-studio.html")
  )
    return;
  const q = new URLSearchParams(root.location.search),
    run = q.get("run"),
    revision = Number(q.get("revision"));
  let resolve,
    reject,
    launched = false,
    count = 0,
    settled = false;
  const launch = new Promise((a, b) => {
    resolve = a;
    reject = b;
  });
  launch.catch(() => {});
  function send(type, data = {}) {
    root.parent.postMessage(
      bridge.message(CHANNEL, run, revision, type, data),
      root.location.origin,
    );
  }
  // Reuses the shell's temporary-profile boot and bounded diagnostics plumbing.
  root.glob2Studio = {
    launch,
    send,
    generator: true,
    watch: false,
    diagnostic(text) {
      if (count++ < 40)
        send("diagnostic", { text: String(text).slice(0, 2000) });
    },
    progress(tick, ended) {
      if (settled) return;
      send(ended ? "complete" : "progress", {
        tick,
        result: ended ? "ended" : "running",
      });
      if (ended) settled = true;
    },
  };
  root.addEventListener("message", (event) => {
    if (
      event.origin !== root.location.origin ||
      event.source !== root.parent ||
      !bridge.envelope(event.data, CHANNEL, run, revision)
    )
      return;
    const m = event.data;
    if (m.type === "stop") {
      send("stopped");
      root.location.replace("about:blank");
      return;
    }
    if (m.type === "watch" && launched) {
      root.glob2Studio.watch = true;
      return;
    }
    if (launched || !validLaunch(m, run, revision)) return;
    launched = true;
    resolve(m);
  });
  if (
    root.parent === root ||
    !/^[0-9a-f-]{36}$/.test(run || "") ||
    !Number.isInteger(revision) ||
    revision < 1
  )
    reject(Error("Open this preview from Generator Studio."));
  else send("ready");
})(globalThis);
