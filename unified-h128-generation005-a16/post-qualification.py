from pathlib import Path
import subprocess,time,json,datetime
D=Path(__file__).resolve().parent;W=Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2');SOURCE='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e'
def state(status,**kw):(D/'POST-QUALIFICATION.json').write_text(json.dumps(dict(status=status,source=SOURCE,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**kw),indent=2)+'\n')
def output(args):return subprocess.check_output(args,cwd=W,text=True).strip()
def run(args,name):
 with (D/name).open('w') as f:r=subprocess.run(args,cwd=W,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:state('FAILED',command=args,returncode=r.returncode,log=name);raise SystemExit(r.returncode)
state('WAITING-MAC-QUALIFICATION')
while True:
 p=D/'MACOS-FINAL-QUALIFICATION.json'
 if p.exists():
  q=json.loads(p.read_text());assert q['source']==SOURCE
  if q['status']=='FAILED':state('FAILED-MAC',details=q);raise SystemExit(1)
  if q['status']=='PASSED':break
 time.sleep(5)
run(['python3','test/check_sim_revision.py','--base','origin/master'],'sim-revision-check.log')
head=output(['git','rev-parse','HEAD'])
if head!=SOURCE:
 changes=output(['git','diff','--name-only',SOURCE,head]).splitlines();assert changes and all(x.startswith('test/fixtures/') for x in changes),'Unexpected new source commits'
changed=output(['git','diff','--name-only']).splitlines();assert all(x.startswith('test/fixtures/') for x in changed),'Unexpected dirty source'
if changed:
 run(['git','add','test/fixtures'],'goldens-stage.log');run(['git','commit','-m','Refresh goldens for the cumulative Adaptive16 refit'],'goldens-commit.log')
head=output(['git','rev-parse','HEAD']);run(['git','push','origin','HEAD:refs/heads/codex/adaptive-resource-gradients'],'source-push.log');remote=output(['git','ls-remote','origin','refs/heads/codex/adaptive-resource-gradients']).split()[0];assert remote==head
state('SOURCE-PUSHED-QUALIFIED-MAC',golden_source=head,sim_revision_check='PASS',macos_binary_sha256=q['binary_sha256'],linux_gate_pending=True)
