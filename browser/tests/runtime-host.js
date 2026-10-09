// Runtime hosts need real HTTP isolation headers. WebKit's intercepted document
// responses can expose shared memory in the page but omit it in pthread workers.
const fs = require('node:fs');
const path = require('node:path');
const {randomUUID} = require('node:crypto');
exports.openRuntimeHost = async (page, body, search = '') => {
  const name = 'test-host-' + randomUUID() + '.html';
  const directory = process.env.GLOB2_TEST_BUILD_DIR || path.resolve(__dirname, '../../build/emscripten/client/release');
  const file = path.resolve(directory, name);
  fs.writeFileSync(file, body);
  try { await page.goto('/' + name + search); }
  finally { fs.unlinkSync(file); }
};
