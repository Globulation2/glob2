// Development-only timeout adaptation. Real test, controls, pixels and assertions
// remain unchanged; use the helper's existing timeout argument on a busy O0 build.
const path=require('node:path');
const helper=require(path.join(process.cwd(),'browser/tests/main-menu.js'));
const original=helper.waitForMainMenu;
helper.waitForMainMenu=(page,options={})=>original(page,{timeout:180000,...options});
