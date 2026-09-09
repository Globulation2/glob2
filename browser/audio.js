// SPDX-License-Identifier: GPL-3.0-or-later
// Invoke directly from a user gesture, while browser activation is still valid.
async function glob2ActivateAudio(context, reportError) {
  if (!context || context.state === 'closed' || context.state === 'running') return;
  try {
    await context.resume();
  } catch (error) {
    // SDL can close the context while a gesture's resume promise is pending.
    // A later gesture may retry other failures; never leave a rejected promise
    // unhandled or claim that audio started when the browser refused it.
    if (context.state !== 'closed') reportError(error);
  }
}
if (typeof module !== 'undefined' && module.exports) module.exports = glob2ActivateAudio;
if (typeof globalThis !== 'undefined') globalThis.glob2ActivateAudio = glob2ActivateAudio;
