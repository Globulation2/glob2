const {test}=require('node:test');const assert=require('node:assert/strict');
const {validLaunch}=require('../set-preview.js');
test('set previews accept only the bound revision and bounded UTF-8 source',()=>{
  const m={channel:'glob2-set-preview',version:1,type:'launch',runId:'test',revision:1,source:'{}'};
  assert.ok(validLaunch(m,'test',1));
  for(const patch of [{revision:2},{channel:'wrong'},{source:'\0'},{source:'é'.repeat(9*1024*1024)},{type:'complete'}])assert.ok(!validLaunch({...m,...patch},'test',1));
});


const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('function browserSetPreviewResult(');
const end = shell.indexOf('// End browser set preview result.', start);
const context = vm.createContext({});
vm.runInContext(shell.slice(start, end), context);
test('rejected asset sets retain validation diagnostics without reading a preview', () => {
  const report = {valid: false, errors: ['Missing terrain image']};
  const calls = [];
  const result = context.browserSetPreviewResult(2, {readFile(file) {
    calls.push(file);
    return JSON.stringify(report);
  }});
  assert.deepEqual(JSON.parse(JSON.stringify(result)), {report, png: null});
  assert.deepEqual(calls, ['/tmp/report.json']);
});
test('operational failures and malformed validation reports are errors', () => {
  assert.throws(() => context.browserSetPreviewResult(3, {readFile() {
    assert.fail('operational failures must not read outputs');
  }}), /exited with code 3/);
  assert.throws(() => context.browserSetPreviewResult(2, {readFile: () => '{}'}), /Invalid set validation report/);
});
