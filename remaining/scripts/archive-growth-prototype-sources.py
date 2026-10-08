from pathlib import Path
import json,hashlib,subprocess
r=Path('artifacts/resource-growth/remaining');manifest=json.load(open(r/'isolated-source-manifest.json'));dest=r/'reproduction'
source=(r/'sources/Map.h').read_text()
source=source.replace('void materialStockChanged(size_t index, MaterialMask before);','void materialStockChanged(size_t index, MaterialMask before);\n    void commitMaterialAmount(size_t index, unsigned material, Uint16 oldAmount, Uint16 amount, const ResourceProperties& properties);')
source=source.replace('void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);','''enum class MaterialDeltaResult { AppliedExisting, AppliedSeed, Rejected, CapacityClamped };
    MaterialDeltaResult applyMaterialDelta(size_t index, Uint16 type, unsigned material, int delta);
    void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);''')
for family,files in manifest.items():
 for name,h in files.items():
  if family=='stock-tracking':data=(r/'c-layout-src'/name).read_bytes()
  elif name=='src/map/Map.h':data=source.encode()
  elif name.endswith('WorldSnapshot.h'):data=(r/'sources/WorldSnapshot.h').read_bytes()
  elif name.endswith('MapResourceState.cpp'):data=(r/'isolated-off-MapResourceState-MapResourceState.cpp').read_bytes()
  elif name.endswith('ResourceGrowth.cpp'):data=(r/'isolated-off-ResourceGrowth-ResourceGrowth.cpp').read_bytes()
  elif name.endswith('WorldCapture.cpp'):data=(r/'isolated-off-WorldCapture-WorldCapture.cpp').read_bytes()
  else:data=Path(name).read_bytes()
  assert hashlib.sha256(data).hexdigest()==h,(family,name)
  out=dest/family/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
(dest/'integrated-baseline.patch').write_bytes(subprocess.check_output(['git','diff','--binary','0f1a2569ab7f23c8702a078978054f73f4ddb9cc','5e9f333f8d6bf89c064ad5ac44d95c1b11e41519']))
print('Both source families match their frozen manifest.')
