// Pointer targets for the bounded NewMapScreen preview and campaign workspace.
// Keep these in one place when their visible layout changes; no game actions
// are invoked through diagnostics, so the tests still exercise hit testing.
exports.clickCreateMap = page => {
  const {width,height}=page.viewportSize();
  const w=Math.min(940,width-24), wide=w>=500;
  const card=Math.min(height-24,84+Math.min(340,wide?w/2-20:w)+28+(wide?0:132)+8+48);
  return page.locator('#canvas').click({position:{x:width/2+w/4,y:(height+card)/2-24},delay:80});
};
exports.campaignFrame = page => {
  const {width,height}=page.viewportSize();
  const w=Math.min(940,width-32),h=Math.min(660,height-24);
  return {x:(width-w)/2,y:(height-h)/2,w,h,footer:(height+h)/2-48};
};
exports.clickCampaignFooter = (page,save) => {
  const f=exports.campaignFrame(page);
  return page.locator('#canvas').click({position:{x:f.x+f.w*(save ? 0.75 : 0.25),y:f.footer+24},delay:80});
};

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
    const toolchain = platform === 'win32' ? 'windows' : platform;
    const binary = path.join(root, `build/${toolchain}/client/release/src/glob2${platform === 'win32' ? '.exe' : ''}`);
    const catalog = JSON.parse(require('node:child_process').execFileSync(
      binary, ['--headless-catalog'], {cwd:root, encoding:'utf8', maxBuffer:8*1024*1024}));
    landscapeNames = catalog.generators.filter(entry => entry.method !== 0)
      .map(entry => entry.nameKey).sort();
  }
  return landscapeNames;
}
exports.chooseEditorLandscape = async (page,name) => {
  const {expect}=require('@playwright/test');
  const canvas=page.locator('#canvas');
  const screen=()=>page.evaluate(()=>glob2Diagnostics.snapshot().screen);
  const names=editorLandscapeNames();
  const index=names.findIndex(label=>label.toLowerCase()===name.toLowerCase());
  if(index<0) throw new Error('Unknown editor landscape fixture: '+name);
  // Browse is the first field on the default generated-map preview card.
  await canvas.click({position:{x:830,y:304},delay:80});
  await expect.poll(screen).toContain('LandscapePickerScreen');
  await canvas.click({position:{x:168,y:100},delay:80}); // Alphabetical.
  for(let i=0;i<names.length;++i) await canvas.press('ArrowLeft');
  for(let i=0;i<index;++i) await canvas.press('ArrowRight');
  // Confirmation is disabled while the selected preview is being generated.
  await expect(async()=>{
    await canvas.press('Enter');
    await expect.poll(screen,{timeout:1000}).toContain('NewMapScreen');
  }).toPass({timeout:60000,intervals:[500,1000]});
};
