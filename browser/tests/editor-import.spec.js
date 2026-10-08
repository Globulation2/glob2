const {clickCreateMap}=require('./editor-controls');
const {clickControl,control,clickMainMenu,gameURL}=require('./main-menu');
const {test,expect}=require('@playwright/test');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);

// Definition imports pick a file from the device through the host file chooser,
// without leaving the editor, and feed its bytes to the same JSON importer.
test('editor imports terrain definitions from a device file',async({page},info)=>{
  test.setTimeout(120000);
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'editor');await screen(page,'EditorMainMenu');
  await clickControl(page,'new-map');await screen(page,'NewMapScreen');
  await clickCreateMap(page);await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'terrain/import');
  // A fresh profile has no definitions folder; the list says so instead of
  // claiming that no files were saved.
  await control(page,'device');
  const definitions={schemaVersion:1,terrains:[{key:'example:device',name:'Device terrain',base:'grass',
    properties:{groundSpeedQ8:192},appearance:'sand'}]};
  const chooser=page.waitForEvent('filechooser');
  await clickControl(page,'device');
  await (await chooser).setFiles({name:'device-terrain.json',mimeType:'application/json',
    buffer:Buffer.from(JSON.stringify(definitions))});
  await expect.poll(async()=>(await state(page)).import,{timeout:60000}).toBe('succeeded');
  await screen(page,'MapEditorScreen');
  // The imported type is offered as a brush once fertility has been recomputed.
  await control(page,'brush/terrain/example:device',{timeout:60000});
  await page.screenshot({path:info.outputPath('editor-device-import.png')});
});

test('an invalid device file keeps the import dialog open',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'editor');await screen(page,'EditorMainMenu');
  await clickControl(page,'new-map');await screen(page,'NewMapScreen');
  await clickCreateMap(page);await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'resource/import');
  const chooser=page.waitForEvent('filechooser');
  await clickControl(page,'device');
  await (await chooser).setFiles({name:'not-definitions.exe',mimeType:'application/octet-stream',buffer:Buffer.from('x')});
  await expect.poll(async()=>(await state(page)).import).toBe('invalid');
  await control(page,'cancel');await screen(page,'MapEditorScreen');
});
