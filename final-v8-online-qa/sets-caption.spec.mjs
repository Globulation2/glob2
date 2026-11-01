import {readFileSync} from 'node:fs';
import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
test('schema captions translate only audited fields and preserve canonical terrain/resource values',async({page,request,baseURL},info)=>{
 const seed=await(await request.get('/__seed')).json();
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.userSession,url:baseURL}]);
 const add=kind=>bg['Add {slot0}'].replace('{slot0}',bg[kind]);
 const group=kind=>page.getByRole('group',{name:bg['{slot0} properties'].replace('{slot0}',bg[kind]),exact:true});
 async function capture(name){
  await page.evaluate(()=>document.fonts.ready);
  expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);
  await info.attach(name,{body:await page.screenshot(),contentType:'image/png'});
 }
 await page.goto('/sets/new');
 await page.getByRole('button',{name:add('Terrain'),exact:true}).click();
 const override=page.getByRole('combobox',{name:bg['Add a property override'],exact:true});
 for(const [id,caption] of [['resourcesGrow','Resource growth enabled'],['buildable','Buildings allowed'],['groundSpeedQ8','Ground movement']]){
  await expect(override.locator('option[value="'+id+'"]')).toHaveText(bg[caption]);await override.selectOption(id);
 }
 await expect(override.locator('option[value="walkable"]')).toHaveText('walkable');await override.selectOption('walkable');
 await expect(group('Terrain').getByRole('checkbox',{name:bg['Resource growth enabled'],exact:true})).toBeChecked();
 await expect(group('Terrain').locator('code').filter({hasText:/^walkable$/})).toBeVisible();
 await group('Terrain').getByRole('checkbox',{name:bg['Resource growth enabled'],exact:true}).scrollIntoViewIfNeeded();
 await capture('terrain-schema-captions');
 const saved=page.waitForResponse(r=>new URL(r.url()).pathname==='/api/v1/set-drafts'&&r.request().method()==='POST');
 await page.getByRole('button',{name:bg['Save draft'],exact:true}).click();const persisted=await(await saved).json();
 expect(persisted.package.terrains[0].properties).toEqual({resourcesGrow:true,buildable:true,groundSpeedQ8:256,walkable:true});
 await page.goto('/sets/new');
 await page.locator('.set-tabs').getByRole('button',{name:bg.Resources,exact:true}).click();await page.getByRole('button',{name:add('Resource'),exact:true}).click();
 const material=group('Resource').getByRole('textbox',{name:bg['Main material'],exact:true});await expect(material).toHaveValue('wood');
 await expect(group('Resource').getByRole('spinbutton',{name:new RegExp('^'+bg['Growth rate'])})).toHaveCount(2);
 await expect(group('Resource').locator('code').filter({hasText:/^ecology$/})).toBeVisible();
 await expect(group('Resource')).not.toContainText('Primary Material');await expect(group('Resource')).not.toContainText('Growth Rate');
 await material.scrollIntoViewIfNeeded();await capture('resource-schema-captions');
});
