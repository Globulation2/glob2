// AI Studio's ephemeral browser host. The generated AI is interpreted by Glob2,
// never evaluated by the web page. This bridge accepts one launch per document.
(function (root) {
  const MAP_HASH =
    "b3cba13a66f18eb2680df431c4e5b0b7bf594b9ada759283af981f3cc15f4a39";
  function validLaunch(m, run, revision) {
    return (
      m &&
      m.channel === "glob2-ai-studio" &&
      m.version === 1 &&
      m.type === "launch" &&
      m.runId === run &&
      m.revision === revision &&
      typeof m.source === "string" &&
      !m.source.includes("\0") &&
      new TextEncoder().encode(m.source).length > 0 &&
      new TextEncoder().encode(m.source).length <= 131072 &&
      Number.isInteger(m.seed) &&
      m.seed >= 0 &&
      m.seed <= 4294967295 &&
      ["numbi", "nicowar"].includes(m.opponent) &&
      m.map instanceof ArrayBuffer &&
      m.map.byteLength > 0 &&
      m.map.byteLength <= 1048576
    );
  }
  if (typeof module !== "undefined" && module.exports)
    module.exports = { validLaunch, MAP_HASH };
  if (!root.location || !root.location.pathname.endsWith("/studio.html"))
    return;
  const query = new URLSearchParams(root.location.search),
    run = query.get("run"),
    revision = Number(query.get("revision"));
  let launched = false,
    count = 0,
    settled = false;
  let resolveLaunch, rejectLaunch;
  const launch = new Promise((resolve, reject) => {
    resolveLaunch = resolve;
    rejectLaunch = reject;
  });
  // A rejected pre-boot launch is observed when the runtime reaches preRun.
  launch.catch(() => {});
  function send(type, data = {}) {
    root.parent.postMessage(
      {
        channel: "glob2-ai-studio",
        version: 1,
        runId: run,
        revision,
        type,
        ...data,
      },
      root.location.origin,
    );
  }
  root.glob2Studio = {
    launch,
    send,
    diagnostic(text) {
      if (count++ < 40)
        send("diagnostic", { text: String(text).slice(0, 2000) });
    },
    progress(tick, ended, disabled, diagnostic, won, lost) {
      if (settled) return;
      const text = String(diagnostic || "").slice(0, 2000);
      send(ended || disabled ? "complete" : "progress", {
        tick,
        disabled: !!disabled,
        diagnostic: text,
        result: disabled
          ? "controller disabled"
          : won
            ? "won"
            : lost
              ? "lost"
              : ended
                ? "ended"
                : "running",
      });
      if (ended || disabled) settled = true;
    },
  };
  root.addEventListener("message", async (event) => {
    if (event.origin !== root.location.origin || event.source !== root.parent)
      return;
    const m = event.data;
    if (
      !m ||
      m.channel !== "glob2-ai-studio" ||
      m.version !== 1 ||
      m.runId !== run ||
      m.revision !== revision
    )
      return;
    if (m.type === "stop") {
      send("stopped");
      root.location.replace("about:blank");
      return;
    }
    if (launched || !validLaunch(m, run, revision)) return;
    launched = true;
    try {
      const digest = Array.from(
        new Uint8Array(await root.crypto.subtle.digest("SHA-256", m.map)),
        (n) => n.toString(16).padStart(2, "0"),
      ).join("");
      if (digest !== MAP_HASH)
        throw Error("Playtest map did not match the pinned fixture.");
      resolveLaunch(m);
    } catch (e) {
      rejectLaunch(e);
      send("error", { text: String(e) });
    }
  });
  if (
    root.parent === root ||
    !/^[0-9a-f-]{36}$/.test(run || "") ||
    !Number.isInteger(revision) ||
    revision < 1
  ) {
    rejectLaunch(Error("Open this playtest from AI Studio."));
  } else send("ready");
})(globalThis);
