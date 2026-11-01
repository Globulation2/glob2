import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {readFileSync,mkdirSync} from 'node:fs';
const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
async function capture(page,info,name){
 await page.evaluate(()=>document.fonts.ready);
 expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth),name).toBeLessThanOrEqual(1);
 await expect(page.locator('html')).toHaveAttribute('lang','bg');
 const out=new URL('./verified/screenshots/',import.meta.url);mkdirSync(out,{recursive:true});
 await page.screenshot({path:new URL(info.project.name+'-'+name+'.png',out).pathname,fullPage:true});
 await info.attach(name,{body:await page.screenshot({fullPage:true}),contentType:'image/png'});
}
test('new skin model and material captions use Bulgarian catalog',async({page,baseURL},info)=>{
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);
 await page.goto('/skins',{waitUntil:'networkidle'});
 await expect(page.getByLabel(bg['Skin name'],{exact:true})).toBeEnabled();
 for(const key of ['Worker','Warrior','Explorer','Swarm'])await expect(page.getByRole('button',{name:bg[key],exact:true})).toBeVisible();
 await page.getByRole('button',{name:bg.Material,exact:true}).click();
 for(const key of ['Classic glossy','Matte','Riveted metal','Hairy','Woven fabric'])await expect(page.getByRole('radio',{name:bg[key],exact:true})).toBeAttached();
 await capture(page,info,'new-skin-materials');
});
test('new administrative tabs and finance filters render Bulgarian on real seeded API',async({page,request,baseURL},info)=>{
 const seed=await(await request.get('/__seed')).json();
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.adminSession,url:baseURL}]);
 for(const route of ['finances','accounts','audit']){
  await page.goto('/admin/'+route,{waitUntil:'networkidle'});
  await expect(page.getByRole('heading',{name:bg.Administration,exact:true})).toBeVisible();
  for(const key of ['Finances','Accounts','Audit'])await expect(page.getByRole('link',{name:bg[key],exact:true})).toBeVisible();
  await capture(page,info,'new-admin-'+route);
 }
});
