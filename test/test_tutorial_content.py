"""Content authoring contracts; no game build needed."""
import copy
import importlib.util
import json
import os
import runpy
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tutorial_builder',ROOT/'tools/tutorials/build.py')
builder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

class TutorialContentTest(unittest.TestCase):
    def setUp(self):
        self.lesson=json.loads((ROOT/'content/scripts/tutorials/introduction.json').read_text())
    def test_catalog_bundles_are_current(self):
        for entry in json.loads((ROOT/'content/scripts/tutorials/catalog.json').read_text()):
            definition=json.loads((ROOT/entry['definition']).read_text())
            self.assertEqual(builder.bundle(definition),(ROOT/entry['script']).read_text())

    def test_shipped_lessons_have_no_unreachable_target_definitions(self):
        # Hooks may refer to targets through code; ordinary shipped definitions
        # should only retain targets reachable from authored behavior.
        from tutorials.content import catalog_lessons
        for entry,definition in catalog_lessons(ROOT):
            if definition.get('hooks'):
                continue
            reachable=set()
            def visit(name):
                if name in reachable:
                    return
                reachable.add(name)
                target=definition['targets'][name]
                for key in ('from','within','assignedTo'):
                    if target.get(key):
                        visit(target[key])
            def roots(value):
                if isinstance(value,dict):
                    for key,item in value.items():
                        if key in ('target','area','destination','buildingTarget') and isinstance(item,str):
                            visit(item)
                        else:
                            roots(item)
                elif isinstance(value,list):
                    for item in value:
                        roots(item)
            for key in ('steps','recovery','onStart'):
                roots(definition.get(key,[]))
            self.assertEqual(set(definition['targets']),reachable,entry['definition'])
    def test_dangling_and_cyclic_targets_are_rejected(self):
        for change in ('missing','cycle'):
            d=copy.deepcopy(self.lesson)
            d['targets']['newInns']['from']='absent' if change=='missing' else 'finishedInns'
            with self.assertRaises(ValueError): builder.validate(d)
    def test_unknown_conditions_anchors_and_identifiers_are_rejected(self):
        for field,value in [('id','complete'),('complete',{'type':'typo'}),('highlights',['typo'])]:
            d=copy.deepcopy(self.lesson);d['steps'][0][field]=value
            with self.assertRaises(ValueError): builder.validate(d)
    def test_false_improvement_and_unresolved_encounter_actions_are_rejected(self):
        d=copy.deepcopy(self.lesson)
        d['steps'][0]['complete']={'type':'increased','target':'workers','field':'hp','delta':-1}
        with self.assertRaises(ValueError): builder.validate(d)
        d=copy.deepcopy(self.lesson)
        d['steps'][0]['onEnter']=[{'type':'scenarioOrder','team':1,'order':{'type':'moveFlag','destination':'missing'}}]
        with self.assertRaises(ValueError): builder.validate(d)
    def test_source_and_debug_packages_keep_lesson_definitions_and_scripts(self):
        class Environment(dict):
            def Install(self, destination, source):
                installed.append((destination,source))
            def Alias(self, *args): pass
        for target in ('dist','install'):
            packed=[];installed=[];old=Path.cwd()
            try:
                os.chdir(ROOT/'scripts')
                runpy.run_path('SConscript',init_globals={'env':Environment(TARFILE='source.tar',INSTALLDIR='prefix'),
                    'Import':lambda *args:None,'PackTar':lambda archive,path:packed.append(Path(path).as_posix()),
                    'COMMAND_LINE_TARGETS':[target]})
            finally:
                os.chdir(old)
            self.assertIn('tutorials/catalog.json',packed)
            self.assertIn('tutorials/healing.json',packed)
            self.assertIn('healing.js',packed)
            for part in range(1,5):
                self.assertNotIn(f'tutorial_part{part}.sgsl',packed)
            self.assertTrue(any('tutorials' in dest and source.endswith('catalog.json') for dest,source in installed))
    def test_an_additional_ordinary_step_needs_only_definition_data(self):
        d=copy.deepcopy(self.lesson)
        d['steps'].append(copy.deepcopy(d['steps'][1]));d['steps'][-1]['id']='build-another'
        self.assertEqual(len(builder.validate(d)['steps']),6)

    def test_chinese_review_export_uses_current_lesson_definitions(self):
        spec=importlib.util.spec_from_file_location('lesson_text_export',ROOT/'tools/china/export_review_text.py')
        exporter=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        rows,missing=exporter.rows()
        paths={row[1] for row in rows if row[0]=='tutorial'}
        self.assertEqual(paths,{entry['definition'] for entry in json.loads((ROOT/'content/scripts/tutorials/catalog.json').read_text())})
        self.assertFalse(any(path.endswith('.sgsl') for path in paths))

    def test_invalid_lesson_selection_fails_before_generation_or_bundle_writes(self):
        for options,message in [(['--check','--maps','--lessons','swiming','--binary','/not/a/binary'], 'Unknown lesson IDs: swiming'),
                                (['--lessons','introduction'], '--lessons requires --maps')]:
            result=subprocess.run([sys.executable,str(ROOT/'tools/tutorials/build.py'),*options],
                                  capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn(message,result.stderr)
            self.assertEqual(result.stdout,'')

    def test_source_archive_keeps_the_build_tools_shared_inventory(self):
        packed=[]
        old=Path.cwd()
        try:
            os.chdir(ROOT/'tools')
            runpy.run_path('SConscript',init_globals={'env':{'TARFILE':'source.tar'},
                'Import':lambda *args:None,
                'PackTar':lambda archive,paths:packed.extend(paths if isinstance(paths,list) else [paths]),
                'COMMAND_LINE_TARGETS':['dist']})
        finally:
            os.chdir(old)
        self.assertIn('tutorials/build.py',packed)
        self.assertIn('tutorials/content.py',packed)

    def test_shared_inventory_rejects_empty_catalogs(self):
        from tutorials.content import packaged_assets
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'content/scripts/tutorials').mkdir(parents=True)
            (root/'content/scripts/tutorials/catalog.json').write_text('[]')
            with self.assertRaisesRegex(ValueError,'catalog entries'):
                packaged_assets(root)

if __name__=='__main__': unittest.main()
