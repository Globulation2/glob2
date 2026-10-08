from pathlib import Path
import json
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';allsc=[s['id'] for s in json.load(open(base/'manifest.json'))['scenarios']];primary=['dense','multi','ai512','disabled512']
def variant(name,exe=None,cwd=None,threads=4,delay=8,execution='shared',immediate=False):
 args=['--compute-threads',str(threads)]
 if not immediate:args+=['--resource-growth-delay',str(delay),'--resource-growth-execution',execution]
 return {'name':name,'binary':str(exe or base/name),'cwd':str(cwd or root),'args':args}
baseline=variant('baseline',base/'baseline/glob2');production=variant('production');integratedBase=variant('integration-baseline',cwd=base/'integration-src');integrated=variant('integration-production',cwd=base/'integration-src')
master=variant('latest-master',base/'latest-master-src/build/linux/client/release/src/glob2',base/'latest-master-src',immediate=True)
original=variant('optimized-original',base/'optimized-original-src/build/linux/client/release/src/glob2',base/'optimized-original-src',immediate=True)
direct=variant('integration-direct',cwd=base/'integration-src',execution='owner')
def case(name,ref,candidate,scenarios=allsc,same=True):return {'id':name,'reference':ref,'candidate':candidate,'scenarios':scenarios,'same_behavior':same}
plan={'output':'confirmation','repeats':10,'master_revision':'0f1a2569ab7f23c8702a078978054f73f4ddb9cc','cases':[
 case('baseline-confirmation',baseline,production),case('integration-confirmation',integratedBase,integrated),
 case('latest-master',master,integrated,same=False),case('optimized-original',original,integrated,same=False),
 case('direct-owner',direct,integrated,primary),case('merge-effect',production,integrated,primary)]}
(base/'confirmation-plan.json').write_text(json.dumps(plan,indent=2))
cases=[]
for delay,threads in [(8,1),(8,2),(8,4),(8,8),(1,4),(3,4)]:
 cases.append(case(f'delay-{delay}-threads-{threads}',variant('integration-baseline',cwd=base/'integration-src',delay=delay,threads=threads),variant('integration-production',cwd=base/'integration-src',delay=delay,threads=threads),primary))
(base/'sensitivity-plan.json').write_text(json.dumps({'output':'sensitivity','repeats':10,'affinity_design':'Eight physical cores and their SMT siblings reserved for every case, including the four-thread control. Only delay or executor size changes within this sweep. Compare to its paired integrated baseline, not unpaired four-core timings.','cases':cases},indent=2))
