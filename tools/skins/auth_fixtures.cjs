// Test-only Ed25519 key; never use these credentials outside fixtures.
// Run from the repository root: node tools/skins/auth_fixtures.cjs
const {createPrivateKey,createPublicKey,sign,createHash}=require('node:crypto');
const {writeFileSync}=require('node:fs');
const {deflateSync}=require('node:zlib');

// Deterministic colony-v2 assets: a 512x512 RGB colour atlas with one colour
// per model quadrant (worker, warrior / explorer, swarm) and stripes, and a
// 512x512 greyscale material map whose bands cycle through every material id.
function crc32(bytes){let c,crc=0xffffffff;for(const b of bytes){c=(crc^b)&0xff;for(let k=0;k<8;++k)c=c&1?0xedb88320^(c>>>1):c>>>1;crc=(crc>>>8)^c;}return (crc^0xffffffff)>>>0;}
function chunk(type,data){const length=Buffer.alloc(4);length.writeUInt32BE(data.length);const body=Buffer.concat([Buffer.from(type,'ascii'),data]);const crc=Buffer.alloc(4);crc.writeUInt32BE(crc32(body));return Buffer.concat([length,body,crc]);}
function png(size,channels,pixel){
  const header=Buffer.alloc(13);header.writeUInt32BE(size,0);header.writeUInt32BE(size,4);header[8]=8;header[9]=channels===3?2:0;
  const rows=Buffer.alloc((size*channels+1)*size);
  for(let y=0;y<size;++y){const row=y*(size*channels+1);for(let x=0;x<size;++x)pixel(x,y).forEach((v,i)=>{rows[row+1+x*channels+i]=v;});}
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),chunk('IDAT',deflateSync(rows,{level:9})),chunk('IEND',Buffer.alloc(0))]);
}
const quadrants=[[200,60,50],[60,90,200],[230,190,40],[150,60,170]];
const texture=png(512,3,(x,y)=>{const base=quadrants[(y>=256?2:0)+(x>=256?1:0)];return ((x+y)>>4)&1?base.map(v=>Math.min(255,v+40)):base;});
const material=png(512,1,(x,y)=>[(y>>5)&3]);

const key=createPrivateKey({key:Buffer.from('302e020100300506032b657004220420'+'01'.repeat(32),'hex'),format:'der',type:'pkcs8'});
const jwk={...createPublicKey(key).export({format:'jwk'}),kid:'skin-fixture',alg:'EdDSA',use:'sig'};
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
// Mirrors skinManifestSha256 (platform/apps/api/src/skins/manifest.ts): exactly
// these keys in this order (JSON.stringify), and classic omits swarmMesh.
const manifest=(v,swarmMesh)=>sha(JSON.stringify({skinId:v.skinId,textureSha256:v.textureSha256,materialSha256:v.materialSha256,layout:v.layout,buildingColor:v.buildingColor,...(swarmMesh&&swarmMesh!=='classic'?{swarmMesh}:{}),...(v.swarmViewAngle?{swarmViewAngle:v.swarmViewAngle}:{})}));
const version={id:'11111111-1111-4111-8111-111111111111',skinId:'22222222-2222-4222-8222-222222222222',textureSha256:sha(texture),materialSha256:sha(material),layout:'colony-v2',buildingColor:0x334455};
version.manifestSha256=manifest(version);
const crown={...version,swarmMesh:'crown',manifestSha256:manifest(version,'crown')};
const claims={iss:'https://example.test',aud:'glob2-colony-renderer',sub:'33333333-3333-4333-8333-333333333333',accountId:'33333333-3333-4333-8333-333333333333',matchId:'44444444-4444-4444-8444-444444444444',team:2,iat:1700000000,exp:1700086400,version,buildingColor:0x112233};
if(process.env.SKIN_FIXTURE_ORIGIN)claims.iss=process.env.SKIN_FIXTURE_ORIGIN;
if(process.env.SKIN_FIXTURE_NOW){claims.iat=Number(process.env.SKIN_FIXTURE_NOW);claims.exp=claims.iat+86400;}
const header={alg:'EdDSA',typ:'glob2-colony-skin+jwt',kid:jwk.kid};
function token(c=claims,h=header){const message=[h,c].map(v=>Buffer.from(JSON.stringify(v)).toString('base64url')).join('.');return message+'.'+sign(null,Buffer.from(message),key).toString('base64url');}
// Self-consistent manifests that still violate the v2 contract.
const retired={...version,layout:'colony-v1'};retired.manifestSha256=manifest(retired);
const noMaterial={...version};delete noMaterial.materialSha256;noMaterial.manifestSha256=manifest(noMaterial);
const badMaterial={...version,materialSha256:'XYZ'};badMaterial.manifestSha256=manifest(badMaterial);
const invalid={};
for(const [name,change] of Object.entries({audience:{aud:'glob2-relay'},issuer:{iss:'https://attacker.test'},team:{team:3},match:{matchId:version.id},subject:{sub:version.id},expired:{exp:1699999900,iat:1699999800},future:{iat:1700000100},longLifetime:{exp:1700086401},negativeTeam:{team:-1},fractionalTeam:{team:2.5},color:{buildingColor:0x1000000},
  manifest:{version:{...version,manifestSha256:'00'.repeat(32)}},layout:{version:{...version,layout:'unknown'}},retiredLayout:{version:retired},
  materialHash:{version:{...version,materialSha256:'00'.repeat(32)}},malformedMaterial:{version:badMaterial},missingMaterial:{version:noMaterial},
  notBefore:{nbf:1700000100},
  unknownSwarmMesh:{version:{...version,swarmMesh:'pyramid',manifestSha256:manifest(version,'pyramid')}},
  swarmMeshOutsideManifest:{version:{...version,swarmMesh:'crown'}},
  swarmMeshType:{version:{...crown,swarmMesh:1}}}))invalid[name]=token({...claims,...change});
invalid.type=token(claims,{...header,typ:'glob2-match+jwt'});
invalid.critical=token(claims,{...header,crit:['unsupported']});
invalid.algorithm=token(claims,{...header,alg:'none'});
invalid.unknownKey=token(claims,{...header,kid:'unknown'});
const valid=token();
const swarm={crown:token({...claims,version:crown}),classic:token({...claims,version:{...version,swarmMesh:'classic'}})};
const angleVersion={...crown,swarmViewAngle:127}; angleVersion.manifestSha256=manifest(angleVersion,'crown');
swarm.angle=token({...claims,version:angleVersion});
for(const value of [-1,360,12.5,'90']) invalid['angle'+String(value)]=token({...claims,version:{...angleVersion,swarmViewAngle:value}});
invalid.angleOutsideManifest=token({...claims,version:{...crown,swarmViewAngle:127}});
const teams=Array.from({length:32},(_,team)=>token({...claims,team}));invalid.signature=valid.slice(0,-5)+'AAAAA';
writeFileSync(process.env.SKIN_FIXTURE_OUTPUT||'test/fixtures/skins/authorization.json',JSON.stringify({textureHex:texture.toString('hex'),materialHex:material.toString('hex'),teams,jwks:{keys:[jwk]},claims,valid,swarm,invalid},null,2)+'\n');
