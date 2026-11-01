import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {writeFileSync,mkdirSync} from 'node:fs';
test('final home wrapping and Bulgarian glyphs',async({page,baseURL},info)=>{
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);
 await page.goto('/',{waitUntil:'networkidle'});await expect(page.locator('html')).toHaveAttribute('lang','bg');await page.evaluate(()=>document.fonts.ready);
 await expect(page.locator('.home-body')).toBeVisible();expect(await page.locator('.stat').count()).toBeGreaterThan(0);
 const metrics=await page.evaluate(()=>({width:innerWidth,scrollWidth:document.documentElement.scrollWidth,fontReady:document.fonts.check('16px "Glob2 Bulgarian"'),rows:[...document.querySelectorAll('.stat .k,.home-body .section-head h2,.home-body .section-head a')].map(e=>{const r=document.createRange();r.selectNodeContents(e);return {text:e.textContent,rects:[...r.getClientRects()].map(t=>({left:t.left,right:t.right,top:t.top,bottom:t.bottom}))}})}));
 expect(metrics.scrollWidth).toBeLessThanOrEqual(metrics.width+1);expect(metrics.fontReady).toBe(true);expect(metrics.rows.length).toBeGreaterThan(0);for(const row of metrics.rows){expect(row.rects.length).toBeGreaterThan(0);for(const r of row.rects){expect(r.left).toBeGreaterThanOrEqual(-1);expect(r.right).toBeLessThanOrEqual(metrics.width+1)}}
 const out=new URL('./captures/',import.meta.url);mkdirSync(out,{recursive:true});const png=await page.screenshot({fullPage:true});writeFileSync(new URL(info.project.name+'-home.png',out),png);await info.attach('home',{body:png,contentType:'image/png'});writeFileSync(new URL(info.project.name+'-home-metrics.json',out),JSON.stringify(metrics,null,2)+'\n');
});
