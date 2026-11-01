import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
test('map wallet full messages match visible and accessible counts and standalone dialog category',async({page,request,baseURL},info)=>{
 const seed=await(await request.get('/__seed')).json(),reads=[],writes=[];let available=0;
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.userSession,url:baseURL}]);
 await page.context().route('**/api/v1/instance',async r=>{const response=await r.fetch(),d=await response.json();await r.fulfill({response,json:{...d,features:[...new Set([...(d.features??[]),'map-studio'])]}})});
 await page.context().route('**/api/v1/map-studio/**',r=>{
  const path=new URL(r.request().url()).pathname;if(r.request().method()!=='GET'){writes.push(path);return r.fulfill({status:405,json:{message:'Read-only fixture'}})}
  if(path.endsWith('/account')){reads.push({path,available});return r.fulfill({json:{enabled:true,available,reserved:0,packs:[],usage:[],activeRequest:null}})}
  if(path.endsWith('/threads'))return r.fulfill({json:{items:[{id:'11111111-1111-4111-8111-111111111111',title:'Authored project title'}]}});return r.fulfill({status:404,json:{message:'No such fixture'}})
 });
 const metrics=[];
 for(const count of [0,1,2]){
  available=count;await page.goto('/map-studio',{waitUntil:'networkidle'});await expect(page.locator('html')).toHaveAttribute('lang','bg');await page.evaluate(()=>document.fonts.ready);
  const expected='Кредити за карти: '+count,wallet=page.getByRole('button',{name:expected,exact:true});await expect(wallet).toBeVisible();await expect(wallet).toHaveText(expected);await expect(wallet).toHaveAttribute('aria-label',expected);await expect(wallet).toHaveAttribute('aria-expanded','false');
  await expect(page.getByText('Authored project title',{exact:true})).toBeAttached();
  const bounds=await wallet.evaluate(e=>{const r=e.getBoundingClientRect(),range=document.createRange();range.selectNodeContents(e.querySelector('span'));return{width:r.width,height:r.height,textRects:[...range.getClientRects()].map(t=>({left:t.left,right:t.right,top:t.top,bottom:t.bottom})),left:r.left,right:r.right,top:r.top,bottom:r.bottom}});
  expect(bounds.height).toBeGreaterThanOrEqual(44);for(const r of bounds.textRects){expect(r.left).toBeGreaterThanOrEqual(bounds.left-1);expect(r.right).toBeLessThanOrEqual(bounds.right+1);expect(r.top).toBeGreaterThanOrEqual(bounds.top-1);expect(r.bottom).toBeLessThanOrEqual(bounds.bottom+1)}
  metrics.push({count,expected,visible:await wallet.textContent(),aria:await wallet.getAttribute('aria-label'),bounds});
 }
 const out=new URL('./captures/',import.meta.url);mkdirSync(out,{recursive:true});
 async function shot(name){expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);const png=await page.screenshot();writeFileSync(new URL(info.project.name+'-'+name+'.png',out),png);await info.attach(name,{body:png,contentType:'image/png'})}
 await shot('wallet-balance-2');const wallet=page.getByRole('button',{name:'Кредити за карти: 2',exact:true});await wallet.click();await expect(wallet).toHaveAttribute('aria-expanded','true');const dialog=page.getByRole('dialog',{name:'Кредити за карти',exact:true});await expect(dialog).toBeVisible();await expect(dialog.getByRole('heading',{name:'Кредити за карти',exact:true})).toBeVisible();await shot('wallet-dialog-category');await page.keyboard.press('Escape');await expect(wallet).toHaveAttribute('aria-expanded','false');
 expect([...new Set(reads.map(r=>r.available))]).toEqual([0,1,2]);expect(writes).toEqual([]);await info.attach('wallet-counts-and-callbacks',{body:Buffer.from(JSON.stringify({metrics,reads,writes,scope:'actual MapStudio header/dialog, real seeded session, explicit read-only wallet feature fixture'},null,2)),contentType:'application/json'});writeFileSync(new URL(info.project.name+'-metrics.json',out),JSON.stringify({metrics,reads,writes},null,2)+'\n');
});
