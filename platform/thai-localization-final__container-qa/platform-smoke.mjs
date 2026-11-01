import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
const hash = data => createHash('sha256').update(data).digest('hex');
const result = {node:process.version, uid:process.getuid(), checks:[],files:{}};
assert.equal(process.getuid(),10001);
const i18n = await import('/app/platform/packages/i18n/src/index.ts');
assert.equal(i18n.resolveLocale('th-TH'),'th');
assert.equal(i18n.hasMessage('Sign in','th'),false);
await i18n.setLocale('th');
assert.equal(i18n.hasMessage('Sign in','th'),true);
assert.equal(i18n.t('Sign in'),'ลงชื่อเข้าใช้');
assert.equal(i18n.t('{count} version',{count:2}),'2 เวอร์ชัน');
assert.equal(i18n.resolveLocale('th-Latn'),'en');
result.checks.push('Thai lazy import, th-TH negotiation, wrong-script rejection, translated string and plural');
const server = await import('/app/platform/packages/i18n/src/server.ts');
assert.equal(server.t('Sign in',{},'th'),'ลงชื่อเข้าใช้');
result.checks.push('Server catalog import');
const { default: Fastify }=await import('/app/platform/node_modules/fastify/fastify.js');
const pages=await import('/app/platform/apps/api/src/web/pages.ts');
const app=Fastify();
await pages.pageAssetRoutes(app);
app.get('/qa-page',(request,reply)=>{pages.setPageLocale(request,reply);return pages.sendPage(reply,'Sign in',pages.pageHtml(reply)`<h1>Sign in</h1><p>Choose a username</p>`);});
const response=await app.inject({method:'GET',url:'/qa-page',headers:{'accept-language':'th-TH'}});
assert.equal(response.statusCode,200);assert.match(response.body,/<html lang="th" dir="ltr">/);assert.match(response.body,/ลงชื่อเข้าใช้/);assert.match(response.body,/เลือกชื่อผู้ใช้/);assert.match(response.body,/script-fonts.css/);
result.checks.push('Fastify injection SSR Thai LTR and translated account UI without listening');
for (const file of ['NotoSansThai-Regular.ttf','LICENSE-Noto.txt','script-fonts.css']){
 const asset=await app.inject({method:'GET',url:'/signin/assets/'+file});assert.equal(asset.statusCode,200);assert.ok(asset.rawPayload.length>100);if(file==='script-fonts.css')assert.match(asset.body,/\/signin\/assets\/NotoSansThai-Regular.ttf/);result.files[file]={bytes:asset.rawPayload.length,sha256:hash(asset.rawPayload),contentType:asset.headers['content-type']};
}
result.checks.push('SSR font, rewritten CSS and Noto licence asset routes');
for(const name of ['packages/i18n/locales/th.json','packages/i18n/script-fonts.css']){const bytes=readFileSync('/app/platform/'+name);result.files[name]={bytes:bytes.length,sha256:hash(bytes)};}
await app.close();console.log(JSON.stringify(result,null,2));
