"""Only use after root explicitly hands off a stable final preview ELF/hash."""
from pathlib import Path
import os,sys,json,hashlib,struct,subprocess,shutil,time
R=Path(__file__).resolve().parents[3];Q=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def alloc_proof(path):
 data=path.read_bytes();assert data[:6]==b'\x7fELF\x02\x01','Expected Linux ELF64 little endian'
 eh=struct.unpack_from('<16sHHIQQQIHHHHHH',data,0);phoff,shoff,phsize,phnum,shsize,shnum,strings=eh[5],eh[6],eh[9],eh[10],eh[11],eh[12],eh[13]
 assert phoff+phsize*phnum<=len(data) and shoff+shsize*shnum<=len(data)
 sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*shsize)for i in range(shnum)];names_section=sections[strings];names=data[names_section[4]:names_section[4]+names_section[5]];selected=[]
 for row in sections:
  name=names[row[0]:].split(b'\0',1)[0].decode();typ,flags,addr,offset,size=row[1:6]
  if flags&2:selected.append({'name':name,'type':typ,'flags':flags,'address':addr,'offset':offset,'size':size,'alignment':row[8],'sha256':hashlib.sha256(data[offset:offset+size]if typ!=8 else b'').hexdigest()})
 return {'entry':eh[4],'type':eh[1],'machine':eh[2],'program_headers_sha256':hashlib.sha256(data[phoff:phoff+phsize*phnum]).hexdigest(),'alloc_sections':selected}
def writers(path):
 target=path.stat();found=[];unknown=0
 for proc in Path('/proc').iterdir():
  if not proc.name.isdigit():continue
  try: descriptors=list((proc/'fd').iterdir())
  except OSError:unknown+=1;continue
  for fd in descriptors:
   try:
    st=fd.stat()
    if (st.st_dev,st.st_ino)!=(target.st_dev,target.st_ino):continue
    fields=dict(line.split(':',1)for line in (proc/'fdinfo'/fd.name).read_text().splitlines()if ':'in line);flags=int(fields['flags'].strip(),8)
    if flags&3:found.append({'pid':int(proc.name),'fd':int(fd.name),'flags_octal':oct(flags)})
   except OSError:pass
 return {'writers':found,'process_fd_directories_inaccessible':unknown,'scope':'Exact device/inode and descriptor write flags where /proc access is allowed; inaccessible process descriptors are not asserted checked.'}
assert len(sys.argv)==4,'BINARY EXPECTED_SHA REVISION required'
binary=Path(sys.argv[1]).resolve();expected,revision=sys.argv[2:]
from run_phase import source_guard
frozen=source_guard(revision)
assert sha(binary)==expected and revision.encode()in binary.read_bytes();before_writers=writers(binary);assert not before_writers['writers'];time.sleep(1);assert sha(binary)==expected
out=Q/'binaries';out.mkdir(exist_ok=True);full=out/'TranslationPreviewHarness.full';capture=out/'TranslationPreviewHarness.capture';assert not full.exists()and not capture.exists(),'Never overwrite a handoff'
shutil.copyfile(binary,full);assert sha(full)==expected;shutil.copyfile(full,capture);subprocess.run(['strip','--strip-debug',str(capture)],check=True)
original_proof=alloc_proof(full);capture_proof=alloc_proof(capture);assert original_proof==capture_proof,'Strip changed program headers or allocated bytes';assert sha(binary)==expected;after_writers=writers(binary);assert not after_writers['writers'];full.chmod(0o444);capture.chmod(0o555)
proof={'status':'Actual root-authorized stable ELF copy, not binary execution','revision':revision,'final_source_revision':frozen['final_source_revision'],'root_handoff_source':str(binary),'root_handoff_expected_sha256':expected,'full_preserved':{'path':str(full),'sha256':sha(full),'bytes':full.stat().st_size},'capture_copy':{'path':str(capture),'sha256':sha(capture),'bytes':capture.stat().st_size},'program_headers_and_alloc_sections_equal':True,'elf_proof':original_proof,'writer_checks_before':before_writers,'writer_checks_after':after_writers,'source_unchanged_after_copy':True};(Q/'preview-copy-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
receipt_path=Q/'binary-handoff.json';receipt=json.loads(receipt_path.read_text())if receipt_path.exists()else{'revision':revision,'binaries':{}};assert receipt['revision']==revision;assert capture.name not in receipt['binaries'];receipt['binaries'][capture.name]={'sha256':sha(capture),'source_sha256':expected,'copy_proof':'preview-copy-proof.json'};receipt_path.write_text(json.dumps(receipt,indent=2)+'\n');print(str(capture));print('capture SHA256',sha(capture));print('ALLOC sections',len(original_proof['alloc_sections']),'and all program headers exact')
