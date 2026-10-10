// The editor's screens publish their controls by key (see main-menu.js); no
// game actions are invoked through diagnostics, so the tests still exercise
// hit testing on the real controls.
const {clickControl, clickByLabel, rootBox} = require('./main-menu');
exports.clickCreateMap = page => clickControl(page, 'create');
exports.campaignFrame = page => rootBox(page, 'ok');
exports.clickCampaignFooter = (page, save, options) => clickControl(page, save ? 'ok' : 'cancel', options);

// The editor's generator chooser now uses the shared landscape catalog. Drive
// its real sort/navigation controls; the old form's list coordinates no longer
// select a generator. Read the native catalog so new generators cannot silently
// shift a fixture onto another landscape. Only pointer/key events change the UI.
let landscapeNames;
function editorLandscapeNames() {
  if (!landscapeNames) {
    const path = require('node:path');
    const platform = require('node:os').platform();
    const root = path.resolve(__dirname, '../..');
    const toolchain = platform === 'win32' ? 'mingw' : platform;
    const build = path.resolve(root, process.env.GLOB2_BUILD_DIR || `build/${toolchain}/client/release`);
    const binary = path.join(build, 'src', `glob2${platform === 'win32' ? '.exe' : ''}`);
    const catalog = JSON.parse(require('node:child_process').execFileSync(
      binary, ['info', 'catalog', '--format', 'json'], {cwd:root, encoding:'utf8', maxBuffer:8*1024*1024}));
    landscapeNames = catalog.generators.filter(entry => entry.method !== 0)
      .map(entry => entry.nameKey).sort();
  }
  return landscapeNames;
}
exports.chooseEditorLandscape = async (page,name) => {
  const {expect}=require('@playwright/test');
  const screen=()=>page.evaluate(()=>glob2Diagnostics.snapshot().screen);
  const names=editorLandscapeNames();
  if(!names.some(label=>label.toLowerCase()===name.toLowerCase())) throw new Error('Unknown editor landscape fixture: '+name);
  await clickControl(page,'landscape');
  await expect.poll(screen).toContain('LandscapePickerScreen');
  await clickControl(page,'landscape/sort/1'); // Alphabetical.
  await clickByLabel(page,/^landscape\/\d+$/,name); // The card carries the landscape's name.
  // Confirmation is disabled while the selected preview is being generated.
  // Wait before acting: retrying Enter can reach the resumed parent's Create
  // shortcut after a slow picker transition and start generation a second time.
  await clickControl(page,'landscape/use',{timeout:60000});
  await expect.poll(screen,{timeout:60000}).toContain('NewMapScreen');
};
