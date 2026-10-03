// Runtime hosts need real HTTP isolation headers. WebKit's intercepted document
// responses can expose shared memory in the page but omit it in pthread workers.
const fs = require('node:fs');
const path = require('node:path');
const {randomUUID} = require('node:crypto');
exports.openRuntimeHost = async (page, body) => {
  const name = 'test-host-' + randomUUID() + '.html';
  const file = path.resolve(__dirname, '../../build/emscripten/client/release', name);
  fs.writeFileSync(file, body);
  try { await page.goto('/' + name); }
  finally { fs.unlinkSync(file); }
};
