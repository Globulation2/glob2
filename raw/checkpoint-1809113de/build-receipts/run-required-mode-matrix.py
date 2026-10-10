import fcntl,hashlib,json,os,pathlib,subprocess,xml.etree.ElementTree as ET
root=pathlib.Path.cwd(); destination=root/'artifacts/gpu-offload/native-bcfb57075-required-matrix'
binary=root/'artifacts/gpu-offload/candidates/bcfb57075/glob2-unit-tests'
case='experimental mask and parity bindings preserve every plan across retirement and buffer reuse'
destination.mkdir(exist_ok=True)
metadata={'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'source_sha':'bcfb570758e0b4ad7b8756ee63639e592ab56691','git_tree':'01d93c2baab91d74e938391e42e4d519a5685f0c','case':case,'minimum_successful_assertions':274,'note':'Six plans x three mixed/reused batches, plus required-only probe rejection. A unavailable-device/default-off early return cannot satisfy this count. No timing eligibility.'}
(destination/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n');rows=[]
with (root.parent/'gpu-offload-resource.lock').open('a') as lock:
 fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 for epoch,parity in ((1,0),(0,1),(1,1)):
  for interval in (1,3,8):
   name=f'epoch{epoch}-parity{parity}-interval{interval}';directory=destination/name;directory.mkdir(exist_ok=True)
   env={'GLOB2_OPENCL_DEVICE':'0','GLOB2_OPENCL_ACTIVE_EPOCH':str(epoch),'GLOB2_OPENCL_PARITY_BOUND':str(parity),'GLOB2_OPENCL_CHECK_INTERVAL':str(interval),'GLOB2_OPENCL_POLL_US':'0','GLOB2_OPENCL_PROFILE':'0','GLOB2_GRADIENT_WORKER_NOOP':'0','GLOB2_USER_DATA_DIR':str(directory/'profile')}
   command=[str(binary),'-r=xml','--no-breaks=true','--no-colors=true','-tc='+case,'-ts=OpenCLGradient']
   returncode=0
   if not (directory/'results.xml').exists():
    with (directory/'results.xml').open('w') as out,(directory/'stderr.log').open('w') as err:
     returncode=subprocess.run(command,env=dict(os.environ,**env),cwd=root,stdout=out,stderr=err).returncode
   row={'configuration':name,'command':command,'environment':env,'exit_code':returncode,'valid':False,'timing_eligible':False}
   try:
    document=ET.parse(directory/'results.xml');cases=[case for case in document.iter('TestCase') if case.get('skipped')!='true'];assertions=document.find('.//OverallResultsAsserts')
    row['test_cases']=len(cases);row['assertions']=dict(assertions.attrib) if assertions is not None else {}
    row['valid']=returncode==0 and len(cases)==1 and cases[0].get('name')==case and assertions is not None and int(assertions.get('successes','0'))>=274 and int(assertions.get('failures','-1'))==0
   except Exception as error:row['error']=str(error)
   rows.append(row);(destination/'matrix.json').write_text(json.dumps(rows,indent=2)+'\n');print(name,'valid='+str(row['valid']),'assertions='+str(row.get('assertions')),flush=True)
   if not row['valid']:raise RuntimeError('Required-mode oracle gate failed; all raw evidence retained')
