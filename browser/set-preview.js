// SPDX-License-Identifier: GPL-3.0-or-later
// One isolated, ephemeral engine invocation per preview revision.
(function(root) {
  function validLaunch(m, run, revision) {
    return !!m && m.channel==='glob2-set-preview' && m.version===1 && m.type==='launch' &&
      m.runId===run && m.revision===revision && typeof m.source==='string' && !m.source.includes('\0') &&
      new TextEncoder().encode(m.source).length<=16*1024*1024;
  }
  if(typeof module!=='undefined'&&module.exports)module.exports={validLaunch};
  if(!root.location?.pathname.endsWith('/set-preview.html'))return;
  const q=new URLSearchParams(root.location.search),run=q.get('run'),revision=Number(q.get('revision'));
  let resolveLaunch,rejectLaunch,launched=false;
  const launch=new Promise((resolve,reject)=>{resolveLaunch=resolve;rejectLaunch=reject;});launch.catch(()=>{});
  function send(type,data={}) {root.parent.postMessage({channel:'glob2-set-preview',version:1,runId:run,revision,type,...data},root.location.origin);}
  root.glob2SetPreview={launch,send};
  root.addEventListener('message',e=>{
    if(e.origin!==root.location.origin||e.source!==root.parent||launched||!validLaunch(e.data,run,revision))return;
    launched=true;resolveLaunch(e.data);
  });
  if(root.parent===root||!/^[0-9a-f-]{36}$/.test(run||'')||!Number.isSafeInteger(revision)||revision<1)
    rejectLaunch(Error('Open this preview from the set workspace.'));
  else send('ready');
})(globalThis);
