const {test} = require('node:test');
const assert = require('node:assert/strict');
const Selection = require('../file-selection');
const file=(name,size=3,read=async()=>new Uint8Array(size).buffer)=>({name,size,arrayBuffer:read});
test('accepts selected bytes without treating the extension as format validation',async()=>{
  const selection=new Selection(['game']);
  await selection.select(file('Backup.GAME'));
  assert.equal(selection.state,'selected');
  assert.equal(selection.file.name,'Backup.GAME');
  assert.equal(selection.file.bytes.length,3);
});
test('rejects paths, control characters, unsupported extensions and oversized inputs before reading',async()=>{
  for(const name of ['../a.game','folder/a.game','folder\\a.game','a\0.game','a.game.','a.game ','a.exe','game','.game','C:backup.game','x'.repeat(129)+'.game']) {
    let read=false; const selection=new Selection(['game']);
    await selection.select(file(name,3,async()=>{read=true;return new ArrayBuffer(3);}));
    assert.equal(selection.state,'failed',name); assert.equal(read,false,name);
  }
  for(const size of [0,-1,Infinity,1.5,65*1024*1024]) {
    let read=false; const selection=new Selection(['game']);
    await selection.select(file('a.game',size,async()=>{read=true;return new ArrayBuffer(0);}));
    assert.equal(selection.state,'failed'); assert.equal(read,false);
  }
});
test('rejects changed sizes and read failures',async()=>{
  for(const read of [async()=>new ArrayBuffer(4),async()=>{throw Error('Read failed');}]) {
    const selection=new Selection(['game']);await selection.select(file('a.game',3,read));
    assert.equal(selection.state,'failed');assert.equal(selection.file,null);
  }
});
test('cancellation and disposal cannot publish late file contents',async()=>{
  const cancelled=new Selection(['game']); await cancelled.select(null);
  assert.equal(cancelled.state,'cancelled');
  let finish;const selection=new Selection(['game']);
  const reading=selection.select(file('a.game',3,()=>new Promise(resolve=>finish=resolve)));
  selection.dispose(); finish(new ArrayBuffer(3)); await reading;
  assert.equal(selection.file,null);assert.notEqual(selection.state,'selected');
});

test('a second selection cannot replace an in-flight read',async()=>{
  let finish;const selection=new Selection(['game']);
  const reading=selection.select(file('first.game',3,()=>new Promise(resolve=>finish=resolve)));
  await selection.select(file('second.game'));
  finish(new ArrayBuffer(3));await reading;
  assert.equal(selection.file.name,'first.game');
});
function documentFixture() {
  const handlers={};let removed=0,clicks=0;
  const input={files:[],addEventListener:(name,callback)=>handlers[name]=callback,
    remove:()=>++removed,click:()=>++clicks};
  const document={createElement:()=>input,body:{appendChild:()=>{}}};
  return {document,input,handlers,removed:()=>removed,clicks:()=>clicks};
}
test('picker uses accepted extensions and releases its input on cancellation',async()=>{
  const fixture=documentFixture(),selection=new Selection(['game','map']);
  selection.pick(fixture.document);
  assert.equal(fixture.input.accept,'.game,.map');assert.equal(fixture.clicks(),1);
  fixture.handlers.cancel();
  assert.equal(selection.state,'cancelled');assert.equal(fixture.removed(),1);
});
test('disposing an open picker ignores its late change event',async()=>{
  const fixture=documentFixture(),selection=new Selection(['game']);
  selection.pick(fixture.document);selection.dispose();
  fixture.input.files=[file('late.game')];fixture.handlers.change();
  await Promise.resolve();assert.equal(selection.file,null);
});
