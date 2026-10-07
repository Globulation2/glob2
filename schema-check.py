import ast,json,pathlib,math,subprocess
p=pathlib.Path('test/test_map_report.py');fn=next(n for n in ast.parse(p.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='contract')
schema=json.load(open('docs/map-generators/map-report.schema.json'));ns={'SCHEMA':schema,'math':math};exec(compile(ast.Module(body=[fn],type_ignores=[]),str(p),'exec'),ns)
t= schema['properties']['terrain'];names=json.load(open('tools/terrain_builtin_names.json'));v={name:{'tiles':1,'percent':1.0} for name in set(names+t['required'])}
ns['contract'](v,t);print('PASS all 29 builtin names plus legacy required categories')
old=json.loads(subprocess.check_output(['git','show','origin/master:docs/map-generators/map-report.schema.json']));
try:ns['contract'](v,old['properties']['terrain'])
except AssertionError: print('PASS old schema reproduces rejection')
else:raise AssertionError('old schema accepted new names')
for bad in [dict(v,unknown_typo={'tiles':1,'percent':1}),dict(v,boulders={'tiles':'bad','percent':1}),dict(v,boulders={'tiles':1})]:
 try:ns['contract'](bad,t)
 except AssertionError:pass
 else:raise AssertionError('invalid report accepted')
print('PASS unknown key, invalid count type and missing percent rejected')
