// Shared envelope checks for embedded, temporary-profile coding studios.
(function (root) {
  function envelope(m, channel, run, revision) {
    return (
      !!m &&
      m.channel === channel &&
      m.version === 1 &&
      m.runId === run &&
      m.revision === revision
    );
  }
  function message(channel, runId, revision, type, data = {}) {
    return { ...data, channel, version: 1, runId, revision, type };
  }
  root.glob2StudioEnvelope = { envelope, message };
  if (typeof module !== "undefined" && module.exports)
    module.exports = root.glob2StudioEnvelope;
})(globalThis);
