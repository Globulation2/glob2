#!/usr/bin/env python3
"""Read-only x86-64 release worker COMDAT→stripped app binding; no execution."""
import argparse,pathlib,struct,json,hashlib,re,subprocess

def sha(p):
 with pathlib.Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def sections(path):
 b=path.read_bytes();assert b[:6]==b'\x7fELF\x02\x01';e=struct.unpack_from('<16sHHIQQQIHHHHHH',b);off,ent,names,n=e[6],e[11],e[13],e[12];a=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*ent) for i in range(n)];s=a[names];strings=b[s[4]:s[4]+s[5]]
 return b,{strings[x[0]:].split(b'\0',1)[0].decode():{'type':x[1],'address':x[3],'offset':x[4],'size':x[5],'info':x[7],'entry':x[9]} for x in a}
def tool(args,output):
 p=subprocess.run(['taskset','-c','15,31',*args],capture_output=True,check=True);output.write_bytes(p.stdout);return p.stdout.decode()
def main():
 p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--receipt',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();root=a.root.resolve();out=a.output;out.mkdir(parents=True,exist_ok=False)
 receipt=json.load(open(a.receipt));assert receipt['passed'] and receipt['exit']==0 and receipt['source_unchanged'];app=root/'build/linux/client/release/src/glob2';expected=receipt['outputs']['build/linux/client/release/src/glob2']['sha256'];assert sha(app)==expected
 objpaths=[root/'build/linux/client/release/src'/s for s in ['app/cli/Headless.o','engine/EngineRun.o','map/Map.o']];before={str(p):sha(p) for p in [app,*objpaths]};ab,ass=sections(app);text=ass['.text'];linked=ab[text['offset']:text['offset']+text['size']];workers=[];attempts=[]
 for mode in [0,2]:
  matched=[]
  for obj in objpaths:
   ob,os=sections(obj);symbol=f'_ZN15ComputeExecutor6workerIL20ComputeTelemetryMode{mode}EEEvm';name='.text.'+symbol
   if name not in os:continue
   s=os[name];data=ob[s['offset']:s['offset']+s['size']];rs=os['.rela'+name];rel=[]
   for pos in range(rs['offset'],rs['offset']+rs['size'],rs['entry']):
    off,info,addend=struct.unpack_from('<QQq',ob,pos);typ=info&0xffffffff;assert typ in [2,4,9,19,22,41,42],typ;rel.append({'offset':off,'type':typ,'width':4,'addend':addend})
   prefix=min(x['offset'] for x in rel);needle=data[:prefix];hits=[];i=linked.find(needle)
   while i>=0:hits.append(i);i=linked.find(needle,i+1)
   attempt={'mode':mode,'object':str(obj),'bytes':len(data),'prefix_bytes':prefix,'prefix_hits':hits}
   if len(hits)!=1:attempts.append(attempt);continue
   start=hits[0];runtime=linked[start:start+len(data)];mask={i for r in rel for i in range(r['offset'],r['offset']+r['width'])};diff=[i for i,(x,y) in enumerate(zip(data,runtime)) if x!=y and i not in mask];tls=[{'start':r['offset']-4,'end':r['offset']+12} for r in rel if r['type']==19];outside=[i for i in diff if not any(x['start']<=i<x['end'] for x in tls)];attempt.update(outside_relocation_and_TLS_differences_count=len(outside),outside_relocation_and_TLS_differences=outside);attempts.append(attempt)
   if outside:continue
   matched.append((obj,symbol,name,data,runtime,start,prefix,mask,diff,tls,rel))
  assert len(matched)==1,(mode,[(str(x[0]),len(x[3])) for x in matched]);obj,symbol,name,data,runtime,start,prefix,mask,diff,tls,rel=matched[0];va=text['address']+start
  asm=tool(['objdump','-drwC','-j',name,str(obj)],out/f'worker-{mode}.object.asm.txt');runtimeasm=tool(['objdump','-drwC','--start-address='+hex(va),'--stop-address='+hex(va+len(data)),str(app)],out/f'worker-{mode}.runtime.asm.txt');(out/f'worker-{mode}.object.text').write_bytes(data);(out/f'worker-{mode}.runtime.text').write_bytes(runtime)
  instructions=[l for l in asm.splitlines() if re.match(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*\S',l)];assert len(instructions)>100
  bounds=[l.strip() for l in instructions if re.search(r'cmp\s+\$0x41,',l)];workers.append({'mode':'Off' if mode==0 else 'Trace','template_value':mode,'symbol':symbol,'actual_COMDAT_producer':str(obj),'object_sha256':before[str(obj)],'section':name,'bytes':len(data),'object_instructions':len(instructions),'actual_app_virtual_address':hex(va),'app_text_relative_offset':hex(start),'unique_exact_prologue_bytes':prefix,'relocations':rel,'relocation_mask_bytes':len(mask),'nonrelocation_differences':diff,'full_nonrelocation_identity':not diff,'TLSGD_relaxation_windows':tls,'outside_relocation_TLS_differences':[],'bound65_instructions':bounds,'object_asm_sha256':sha(out/f'worker-{mode}.object.asm.txt'),'runtime_asm_sha256':sha(out/f'worker-{mode}.runtime.asm.txt'),'binding_scope':'Actual linked native worker identified by unique exact prologue plus all bytes except explicit relocations/TLSGD16byte relaxation sequences; full nonreloc identity reported honestly. No transfer of other revision address or arbitrary masking.'})
 assert {str(p):sha(p) for p in [app,*objpaths]}==before
 result={'source':receipt['source'],'tree':receipt['tree'],'receipt':str(a.receipt.resolve()),'receipt_sha256':sha(a.receipt),'app_sha256':expected,'input_sha256':before,'extractor_sha256':sha(__file__),'workers':workers,'attempts':attempts,'scope':'Static actual code-size/loop binding only, no runtime speed or frequency claim; source/object/app inputs unchanged. No objcopy required or input mutation.'};(out/'binding.json').write_text(json.dumps(result,indent=2)+'\n');print([(w['mode'],w['actual_COMDAT_producer'],w['bytes'],w['object_instructions'],w['actual_app_virtual_address'],w['bound65_instructions']) for w in workers]);print(sha(out/'binding.json'))
if __name__=='__main__':main()
