const {test} = require('node:test');
const assert = require('node:assert/strict');
const Storage = require('../storage');
function fixture() {
  const jobs=[], writes=[];
  const storage=new Storage(callback=>writes.push(callback), callback=>jobs.push(callback));
  storage.restored(null);
  return {storage,jobs,writes};
}
test('coalesces writes and acknowledges only the completed generation', async()=>{
  const {storage,jobs,writes}=fixture();
  storage.changed(); storage.changed();
  let firstDone=false, secondDone=false;
  const first=storage.flush().then(()=>firstDone=true);
  assert.equal(jobs.length,1); jobs.shift()();
  const second=storage.flush().then(()=>secondDone=true);
  assert.equal(writes.length,1); assert.equal(jobs.length,0);
  writes.shift()(null); await first;
  assert.equal(firstDone,true); assert.equal(secondDone,false);
  assert.equal(storage.state,'writing'); jobs.shift()();
  writes.shift()(null); await second;
  assert.equal(storage.state,'persisted');
});
test('quota failure is retained, rejects completion, and allows an explicit retry', async()=>{
  const {storage,jobs,writes}=fixture();
  const failure=new Error('Quota exceeded');
  const result=assert.rejects(storage.flush(), /Quota exceeded/);
  jobs.shift()(); writes.shift()(failure); await result;
  assert.equal(storage.state,'failed'); assert.equal(storage.committed,0);
  assert.equal(jobs.length,0);
  const retry=storage.flush(); jobs.shift()(); writes.shift()(null); await retry;
  assert.equal(storage.state,'persisted');
});
test('restore failure cannot overwrite existing storage', async()=>{
  let writes=0;
  const storage=new Storage(()=>++writes);
  storage.restored(new Error('Restore failed'));
  storage.changed(); await assert.rejects(storage.flush(), /Restore failed/);
  assert.equal(writes,0); assert.equal(storage.state,'restore-failed');
});
test('synchronous adapter errors reject the operation', async()=>{
  const jobs=[];
  const storage=new Storage(()=>{throw Error('Database closed');}, callback=>jobs.push(callback));
  storage.restored(null);
  const result=assert.rejects(storage.flush(),/Database closed/);
  jobs.shift()(); await result; assert.equal(storage.state,'failed');
});
