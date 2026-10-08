import {sql} from '../../platform/node_modules/kysely/dist/index.js';
import {createHarness} from '../../platform/apps/api/test/support.ts';
const h=await createHarness();try{const api=await h.start();const a=await h.database.db.insertInto('accounts').values({kind:'registered',display_name:'Performance admin',role:'admin'}).returning('id').executeTakeFirstOrThrow();const secret=await api.app.identity.webSessions.create(a.id);
await sql`INSERT INTO maps(owner_account_id,title,visibility) SELECT ${a.id}::uuid,'Synthetic map '||n,'private' FROM generate_series(1,10000) n`.execute(h.database.db);
await sql`INSERT INTO map_reports(map_id,reporter_account_id,reason) SELECT id,${a.id}::uuid,'other' FROM maps`.execute(h.database.db);
for(const path of ['reports','content?hidden=all','audit','analytics','finances','operations']){const times=[];for(let n=0;n<20;n++){const start=performance.now();const r=await fetch(api.url+'/api/v1/admin/'+path,{headers:{cookie:'glob2_session='+secret}});await r.arrayBuffer();if(r.status!==200)throw Error(path+': '+r.status);times.push(performance.now()-start);}times.sort((a,b)=>a-b);console.log(JSON.stringify({path,rows:10000,samples:20,medianMs:Math.round(times[10]!),p95Ms:Math.round(times[18]!)}));}
}finally{await h.close();}
