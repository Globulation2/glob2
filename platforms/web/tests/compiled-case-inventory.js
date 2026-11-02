const {expect} = require('@playwright/test');
const {openRuntimeHost} = require('./runtime-host');

exports.compiledSuiteInventory = async (page, variant, suite) => {
  await openRuntimeHost(page, `<!doctype html><canvas id="canvas"></canvas><script>
    var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
      locateFile:name=>name.endsWith('.data')?'/'+name:'/${variant === 'threaded' ? 'threaded/' : ''}'+name,
      preRun:[()=>FS.mkdirTree('/evidence')],
      async onRuntimeInitialized(){try {
        const exit=(await Module.start(${JSON.stringify(['--test-suite=' + suite, '--list-test-cases', '--reporters=xml', '--out=/evidence/inventory.xml'])}))??0;
        window.inventory={exit,xml:FS.readFile('/evidence/inventory.xml',{encoding:'utf8'})};
      }catch(error){window.inventory={error:String(error)};}}
    };</script><script src="/${variant === 'threaded' ? 'threaded/' : ''}script-tests.js"></script>`);
  await page.waitForFunction(() => window.inventory, null, {timeout:150000});
  const result=await page.evaluate(() => window.inventory);
  expect(result.error).toBeUndefined();
  expect(result.exit).toBe(0);
  return result.xml;
};

exports.compareCaseInventory = async (page, listing, junit) => {
  const inventories=await page.evaluate(({listing,junit}) => {
    function names(xml,tag) {
      const document=new DOMParser().parseFromString(xml,'application/xml');
      if(document.querySelector('parsererror'))throw new Error('Invalid case inventory XML');
      return Array.from(document.querySelectorAll(tag),node=>node.getAttribute('name')).sort();
    }
    return {selected:names(listing,'TestCase'),completed:names(junit,'testcase')};
  },{listing,junit});
  expect(inventories.selected.length).toBeGreaterThan(0);
  expect(inventories.completed).toEqual(inventories.selected);
  return inventories;
};
