const {test}=require('node:test');const assert=require('node:assert/strict');
const {validLaunch}=require('../set-preview.js');
test('set previews accept only the bound revision and bounded UTF-8 source',()=>{
  const m={channel:'glob2-set-preview',version:1,type:'launch',runId:'test',revision:1,source:'{}'};
  assert.ok(validLaunch(m,'test',1));
  for(const patch of [{revision:2},{channel:'wrong'},{source:'\0'},{source:'é'.repeat(9*1024*1024)},{type:'complete'}])assert.ok(!validLaunch({...m,...patch},'test',1));
});
