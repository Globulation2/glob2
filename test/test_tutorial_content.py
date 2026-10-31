"""Content authoring contracts; no game build needed."""
import copy
import importlib.util
import json
import os
import runpy
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tutorial_builder',ROOT/'tools/tutorials/build.py')
builder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

class TutorialContentTest(unittest.TestCase):
    def setUp(self):
        self.lesson=json.loads((ROOT/'scripts/tutorials/introduction.json').read_text())
    def test_catalog_bundles_are_current(self):
        for entry in json.loads((ROOT/'scripts/tutorials/catalog.json').read_text()):
            definition=json.loads((ROOT/entry['definition']).read_text())
            self.assertEqual(builder.bundle(definition),(ROOT/entry['script']).read_text())
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
            self.assertIn('tutorial_part1.sgsl',packed)
            self.assertTrue(any('tutorials' in dest and source.endswith('catalog.json') for dest,source in installed))
    def test_an_additional_ordinary_step_needs_only_definition_data(self):
        d=copy.deepcopy(self.lesson)
        d['steps'].append(copy.deepcopy(d['steps'][1]));d['steps'][-1]['id']='build-another'
        self.assertEqual(len(builder.validate(d)['steps']),6)

if __name__=='__main__': unittest.main()
