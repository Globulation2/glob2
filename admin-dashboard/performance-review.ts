import {sql} from '../../platform/node_modules/kysely/dist/index.js';
import {createHarness} from '../../platform/apps/api/test/support.ts';
const h=await createHarness();try{const api=await h.start();const a=await h.database.db.insertInto('accounts').values({kind:'registered',display_name:'Performance admin',role:'admin'}).returning('id').executeTakeFirstOrThrow();const secret=await api.app.identity.webSessions.create(a.id);
await sql`INSERT INTO maps(owner_account_id,title,visibility) SELECT ${a.id}::uuid,'Synthetic map '||n,'private' FROM generate_series(1,10000) n`.execute(h.database.db);
await sql`INSERT INTO map_reports(map_id,reporter_account_id,reason) SELECT id,${a.id}::uuid,'other' FROM maps`.execute(h.database.db);

for (const prefix of ['map','music','terrain','building','ai_studio','hive']) {
  await sql`INSERT INTO ${sql.table(prefix+'_ledger')}(id,account_id,amount,kind,created_at)
    SELECT ${prefix}||':'||n,${a.id}::uuid,1,'grant',now()-(n % 365)*interval '1 day' FROM generate_series(1,10000) n`.execute(h.database.db);
}
await sql`INSERT INTO admin_financial_events(id,product,purchase_id,mode,currency,kind,amount,occurred_at)
  SELECT 'perf:'||n,'maps','perf:'||n,'live','usd','payment',100,now()-(n % 365)*interval '1 day'
  FROM generate_series(1,10000) n`.execute(h.database.db);
await sql`WITH owners AS (
  INSERT INTO accounts(kind,display_name) SELECT 'guest','Synthetic '||n FROM generate_series(1,10000) n RETURNING id
), threads AS (
  INSERT INTO studio_threads(account_id,title) SELECT id,'Synthetic operation' FROM owners RETURNING id,account_id
) INSERT INTO studio_requests(id,thread_id,account_id,kind,status,input)
  SELECT gen_random_uuid(),id,account_id,'chat','uncertain','{}'::jsonb FROM threads`.execute(h.database.db);
await sql`ANALYZE map_ledger`.execute(h.database.db);
const plan = await sql`EXPLAIN (ANALYZE,BUFFERS,FORMAT JSON)
  SELECT kind,sum(amount) FROM map_ledger WHERE created_at>=now()-interval '30 days' GROUP BY kind`.execute(h.database.db);
console.log(JSON.stringify({creditLedgerDatePlan:plan.rows}));
for(const path of ['reports','content?hidden=all','audit','analytics','finances','operations']){const times=[];for(let n=0;n<20;n++){const start=performance.now();const r=await fetch(api.url+'/api/v1/admin/'+path,{headers:{cookie:'glob2_session='+secret}});await r.arrayBuffer();if(r.status!==200)throw Error(path+': '+r.status);times.push(performance.now()-start);}const coldMs=Math.round(times[0]!);times.sort((a,b)=>a-b);console.log(JSON.stringify({path,reports:10000,content:10000,ledgerRows:60000,paymentEvents:10000,uncertainRequests:10000,samples:20,coldMs,aggregateCache:'analytics/finances include warm cache hits',medianMs:Math.round(times[10]!),p95Ms:Math.round(times[18]!)}));}
}finally{await h.close();}
