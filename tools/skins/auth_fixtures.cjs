// Test-only Ed25519 key; never use these credentials outside fixtures.
const {createPrivateKey,createPublicKey,sign,createHash}=require('node:crypto');
const {writeFileSync,readFileSync}=require('node:fs');
const texture=readFileSync('platform/apps/api/assets/skins/stripes.png');
const key=createPrivateKey({key:Buffer.from('302e020100300506032b657004220420'+'01'.repeat(32),'hex'),format:'der',type:'pkcs8'});
const jwk={...createPublicKey(key).export({format:'jwk'}),kid:'skin-fixture',alg:'EdDSA',use:'sig'};
const version={id:'11111111-1111-4111-8111-111111111111',skinId:'22222222-2222-4222-8222-222222222222',textureSha256:createHash('sha256').update(texture).digest('hex'),layout:'colony-v1',buildingColor:0x334455};
version.manifestSha256=createHash('sha256').update(JSON.stringify({skinId:version.skinId,textureSha256:version.textureSha256,layout:version.layout,buildingColor:version.buildingColor})).digest('hex');
const claims={iss:'https://example.test',aud:'glob2-colony-renderer',sub:'33333333-3333-4333-8333-333333333333',accountId:'33333333-3333-4333-8333-333333333333',matchId:'44444444-4444-4444-8444-444444444444',team:2,iat:1700000000,exp:1700086400,version,buildingColor:0x112233};
if(process.env.SKIN_FIXTURE_ORIGIN)claims.iss=process.env.SKIN_FIXTURE_ORIGIN;
if(process.env.SKIN_FIXTURE_NOW){claims.iat=Number(process.env.SKIN_FIXTURE_NOW);claims.exp=claims.iat+86400;}
const header={alg:'EdDSA',typ:'glob2-colony-skin+jwt',kid:jwk.kid};
function token(c=claims,h=header){const message=[h,c].map(v=>Buffer.from(JSON.stringify(v)).toString('base64url')).join('.');return message+'.'+sign(null,Buffer.from(message),key).toString('base64url');}
const invalid={};
for(const [name,change] of Object.entries({audience:{aud:'glob2-relay'},issuer:{iss:'https://attacker.test'},team:{team:3},match:{matchId:version.id},subject:{sub:version.id},expired:{exp:1699999900,iat:1699999800},future:{iat:1700000100},longLifetime:{exp:1700086401},negativeTeam:{team:-1},fractionalTeam:{team:2.5},color:{buildingColor:0x1000000},manifest:{version:{...version,manifestSha256:'00'.repeat(32)}},layout:{version:{...version,layout:'unknown'}},notBefore:{nbf:1700000100}}))invalid[name]=token({...claims,...change});
invalid.type=token(claims,{...header,typ:'glob2-match+jwt'});
invalid.critical=token(claims,{...header,crit:['unsupported']});
invalid.algorithm=token(claims,{...header,alg:'none'});
invalid.unknownKey=token(claims,{...header,kid:'unknown'});
const valid=token();
const teams=Array.from({length:32},(_,team)=>token({...claims,team}));invalid.signature=valid.slice(0,-5)+'AAAAA';
writeFileSync(process.env.SKIN_FIXTURE_OUTPUT||'test/fixtures/skins/authorization.json',JSON.stringify({textureHex:texture.toString('hex'),teams,jwks:{keys:[jwk]},claims,valid,invalid},null,2)+'\n');
