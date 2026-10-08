import AxeBuilder from '../../platform/node_modules/@axe-core/playwright/dist/index.mjs';
import { chromium } from '../../platform/node_modules/playwright-core/index.mjs';
import fs from 'node:fs';
const out = new URL('./', import.meta.url).pathname;
const browser = await chromium.launch({headless:true});
const id='11111111-1111-4111-8111-111111111111', draftId='22222222-2222-4222-8222-222222222222';
const messages=Array.from({length:12},(_,i)=>({id:'m'+i,role:i%2?'assistant':'user',text:i%2?'I can refine the design and keep the existing gameplay behavior. Tell me which part you would like to change.':'Create a woodland colony with mushroom buildings and broad paths.',created_at:`2026-10-08T12:00:${String(i).padStart(2,'0')}Z`}));
const buildingSpec=fs.readFileSync('platform/apps/web/e2e/ai-building-studio.spec.ts','utf8');
const terrainSpec=fs.readFileSync('platform/apps/web/e2e/terrain-studio.spec.ts','utf8');
function extractPack(source){const value=source.slice(source.indexOf('  const pack = ')+15,source.indexOf('\n  const writes:'));return Function('id','draftId','return '+value)(id,draftId);}
const buildingPack=extractPack(buildingSpec),terrainPack=extractPack(terrainSpec);
const source='export function metadata() { return { apiVersion: 2, name: "My Colony" }; }\nexport function step(ctx) {\n  const buildings = ctx.game.buildings({team: ctx.myTeam});\n  for (const building of buildings) building.workers = 2;\n}\n';
const current={revision:1,source,hash:'a'.repeat(64),reason:'initial',created_at:'2026-10-08T12:00:00Z'};
const results=[];
for(const scenario of ['enlarged-text','forced-colors','disabled-zero','short-keyboard']){
for(const theme of ['light','dark']){
for(const viewport of [scenario==='short-keyboard'?{width:412,height:420}:{width:1280,height:720}]){
for(const kind of ['map-studio','music-studio','ai-studio','ai-building-studio','terrain-studio']){
 const context=await browser.newContext({viewport,colorScheme:theme,reducedMotion:'reduce'});
 const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{class Events{constructor(){queueMicrotask(()=>this.onopen?.());}close(){}addEventListener(){}};globalThis.EventSource=Events;});
 await page.route('**/api/v1/**',async route=>{
  const path=new URL(route.request().url()).pathname;
  let json={};
  if(path.endsWith('/accounts/me'))json={id,displayName:'Studio author',kind:'registered',role:'user',status:'active',identities:[],entitlements:[]};
  else if(path.endsWith('/instance'))json={name:'Globulation 2',queues:[],origin:'https://example.test',realtimeUrl:'wss://example.test/realtime',supportedSimVersions:[],authProviders:[],guestsAllowed:true,features:[]};
  else if(path.endsWith('/account'))json={enabled:scenario!=='disabled-zero',available:scenario==='disabled-zero'?0:3,balance:scenario==='disabled-zero'?0:3,reserved:0,packs:[],usage:[],model:'Coding model',maxRequestCredits:100,rate:{input:10,cachedInput:1,output:20}};
  else if(path.endsWith('/checks')||path.endsWith('/threads')||path.endsWith('/projects'))json={items:[]};
  else if(path.endsWith('/events'))json={events:[]};
  else if(path.endsWith('/progress'))json={requestId:'old',stages:[],notes:[],checks:[],artifacts:[],historical:false};
  else if(path.includes('/building-drafts/'))json={id:draftId,revision:id,name:'Mushroom hospital',package:buildingPack};
  else if(path.includes('/set-drafts/'))json={id:draftId,revision:1,package:terrainPack,publishedVersionId:null,validation:{status:'valid'}};
  else if(path.includes('/ai-studio/projects/'))json={id,title:'My colony AI',revision:1,current,revisions:[current],requests:messages.filter(m=>m.role==='user').map((m,i)=>({id:m.id,prompt:m.text,response:messages[i*2+1].text,status:'completed',charged:1})),cursor:'0'};
  else json={id,title:'Woodland colony',draftId,cursor:'0',messages,requests:[],references:[],revisions:[],history:{}};
  if(path.includes('/assets/'))return route.fulfill({contentType:'image/png',body:fs.readFileSync('data/gfx/inn0b0.png')});
  return route.fulfill({json});
 });
 await page.goto('http://127.0.0.1:5178/'+kind+'/'+id);
 if(scenario==='forced-colors')await page.emulateMedia({forcedColors:'active'});
 if(scenario==='enlarged-text')await page.addStyleTag({content:'html{font-size:200% !important}'});
 await page.locator('textarea').first().waitFor({timeout:30000});
 await page.getByText('I can refine the design and keep the existing gameplay behavior. Tell me which part you would like to change.', {exact:true}).first().waitFor();
 await page.evaluate(()=>document.fonts.ready);
 if(kind==='ai-studio' && viewport.width>760) await page.locator('.monaco-editor').first().waitFor();
 const metrics=await page.evaluate(()=>{const sel=['.studio-workspace'];const workspace=document.querySelector(sel.join(','));const textarea=document.querySelector('textarea');const rect=el=>{const r=el?.getBoundingClientRect();return r?{x:Math.round(r.x),y:Math.round(r.y),width:Math.round(r.width),height:Math.round(r.height),bottom:Math.round(r.bottom)}:null;};return {viewport:{width:innerWidth,height:innerHeight},document:{width:document.documentElement.scrollWidth,height:document.documentElement.scrollHeight},workspace:rect(workspace),composer:rect(textarea),bodyText:document.body.innerText.slice(-1000)};});
 const axe = await new AxeBuilder({page}).analyze();
 const file=`${scenario}-${kind}-${theme}.png`;await page.screenshot({path:out+file});
 results.push({scenario,kind,theme,file,...metrics,errors,violations:axe.violations});await context.close();
}}}}
fs.writeFileSync(out+'extended-metrics.json',JSON.stringify(results,null,2));console.log(JSON.stringify(results.map(({kind,theme,viewport,document,composer,errors,violations})=>({kind,theme,viewport,document,composer,errors,violations:violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)}))})),null,2));await browser.close();
