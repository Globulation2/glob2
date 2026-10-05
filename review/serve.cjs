const fs=require('node:fs'),http=require('node:http'),crypto=require('node:crypto'),path=require('node:path');
const root=require('node:path').join(__dirname,'..'),origin='http://127.0.0.1:8767',matchId='44444444-4444-4444-8444-444444444444';
const key=crypto.createPrivateKey({key:Buffer.from('302e020100300506032b657004220420'+'01'.repeat(32),'hex'),format:'der',type:'pkcs8'});
const jwk={...crypto.createPublicKey(key).export({format:'jwk'}),kid:'skin-fixture',alg:'EdDSA',use:'sig'};
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
(async()=>{
const sharp=require(process.env.GLOB2_PLATFORM_DIR+'/node_modules/sharp');
const variants=[['input','current-export'],['black-input','current-black-export'],['green-input','current-green-export']].map(([input,output],i)=>{
 const source=JSON.parse(fs.readFileSync(path.join(root,input,'manifest.json'))), bytes=fs.readFileSync(path.join(root,output,'manifest.json')), bundle=JSON.parse(bytes);
 return {version:{...source,id:'11111111-1111-4111-8111-'+String(111111111111+i)},bytes,bundle,input:path.join(root,input),output:path.join(root,output),descriptor:{manifestSha256:hash(bytes),format:bundle.format,renderRevision:bundle.renderRevision}};
});
for(const variant of variants) {
 const source=variant.version;
 variant.texture=await sharp(fs.readFileSync(path.join(variant.input,'texture.png'))).webp({lossless:true,effort:4}).toBuffer();
 variant.material=await sharp(fs.readFileSync(path.join(variant.input,'material.png'))).webp({lossless:true,effort:4}).toBuffer();
 variant.descriptor.source={manifestSha256:source.manifestSha256,textureSha256:source.textureSha256,materialSha256:source.materialSha256};
 const wire={...source,textureSha256:hash(variant.texture),materialSha256:hash(variant.material)};
 wire.manifestSha256=hash(JSON.stringify({skinId:wire.skinId,textureSha256:wire.textureSha256,materialSha256:wire.materialSha256,layout:wire.layout,buildingColor:wire.buildingColor,...(wire.swarmMesh&&wire.swarmMesh!=='classic'?{swarmMesh:wire.swarmMesh}:{}),...(wire.swarmViewAngle?{swarmViewAngle:wire.swarmViewAngle}:{})}));
 variant.version=wire;
}
const now=Math.floor(Date.now()/1000);
const colonySkins=Array.from({length:8},(_,team)=>{
 const v=variants[team%3],c={iss:origin,aud:'glob2-colony-renderer',sub:'33333333-3333-4333-8333-333333333333',accountId:'33333333-3333-4333-8333-333333333333',matchId,team,iat:now,exp:now+86400,version:v.version,buildingColor:v.version.buildingColor,softwareSprites:v.descriptor};
 const body=[{alg:'EdDSA',typ:'glob2-colony-skin+jwt',kid:jwk.kid},c].map(v=>Buffer.from(JSON.stringify(v)).toString('base64url')).join('.');
 return {team,assertion:body+'.'+crypto.sign(null,Buffer.from(body),key).toString('base64url')};
});
fs.writeFileSync(path.join(root,'assignment.json'),JSON.stringify({origin,matchId,colonySkins}));
const counts={};
http.createServer((req,res)=>{
 try {
 let bytes,type='application/json';
 if(req.url==='/.well-known/jwks.json')bytes=Buffer.from(JSON.stringify({keys:[jwk]}));
 else if(req.url===`/api/v1/matches/${matchId}/skins`)bytes=Buffer.from(JSON.stringify({colonySkins}));
 else {
 const parts=new URL(req.url,origin).pathname.split('/'),v=variants.find(v=>v.version.id===parts[5]);if(!v)throw Error('version');
 if(parts[6]==='texture'||parts[6]==='material'){bytes=v[parts[6]];type='image/webp';}
 else if(parts[6]==='sprites'&&parts[7]===v.descriptor.manifestSha256){
  if(parts[8]==='manifest')bytes=v.bytes;
  else if(parts[8]==='pages'&&v.bundle.pages.some(p=>p.sha256===parts[9])){bytes=fs.readFileSync(path.join(v.output,parts[9]+'.webp'));type='image/webp';}
 }
 }
 if(!bytes)throw Error('route');counts[req.url]=(counts[req.url]||0)+1;fs.writeFileSync(path.join(root,'requests.json'),JSON.stringify(counts));res.writeHead(200,{'Content-Type':type});res.end(bytes);
 }catch(e){res.writeHead(404);res.end(String(e));}
}).listen(8767,'127.0.0.1',()=>console.log('READY'));

})().catch(error=>{console.error(error);process.exitCode=1;});
