// Runtime hosts need real HTTP isolation headers. WebKit's intercepted document
// responses can expose shared memory in the page but omit it in pthread workers.
const fs = require('node:fs');
const path = require('node:path');
const {randomUUID} = require('node:crypto');
// Playwright's WebKit occasionally never commits a navigation from about:blank to a
// cross-origin-isolated document: the server answers, but the page stays blank and
// requests nothing more, so goto waits out the whole test. A host document commits
// in well under a second, so retry one that has not committed within this bound.
const COMMIT_TIMEOUT = 20000;
const COMMIT_ATTEMPTS = 2;
exports.openRuntimeHost = async (page, body, search = '') => {
  const name = 'test-host-' + randomUUID() + '.html';
  const directory = process.env.GLOB2_TEST_BUILD_DIR || path.resolve(__dirname, '../../../build/emscripten/client/release');
  const file = path.resolve(directory, name);
  fs.writeFileSync(file, body);
  try {
    for (let attempt = 1; ; ++attempt) {
      try {
        await page.goto('/' + name + search, {waitUntil: 'commit', timeout: COMMIT_TIMEOUT});
        break;
      } catch (error) {
        if (attempt >= COMMIT_ATTEMPTS || error.name !== 'TimeoutError') throw error;
        console.warn(`Runtime host navigation did not commit within ${COMMIT_TIMEOUT} ms; retrying`);
      }
    }
    await page.waitForLoadState('load');
  } finally { fs.unlinkSync(file); }
};
