import os,subprocess,xml.etree.ElementTree as ET
root=ET.parse('artifacts/fertility-128/focused.xml')
command=['python3','test/run_tests.py','--binary','engine','-j2','--timeout','600','--junit','artifacts/fertility-128/retry.xml']
for case in root.iter('testcase'):
    if case.find('failure') is not None or case.find('error') is not None:
        name=case.attrib['name'].split(' [')[0]
        command+=['--filter',case.attrib['classname']+'/'+name+'*']
print(command,flush=True)
with open('artifacts/fertility-128/retry.log','w') as log:
    result=subprocess.run(command,env=dict(os.environ,GLOB2_BUILD_DIR='build/darwin/client/debug/dev-dev_fast-true-linker-auto'),stdout=log,stderr=subprocess.STDOUT)
raise SystemExit(result.returncode)
