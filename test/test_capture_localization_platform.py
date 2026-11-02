import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('capture_localization_platform',ROOT/'tools/capture_localization_platform.py')
capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)

class PlatformCaptureContracts(unittest.TestCase):
    def test_windows_crlf_native_catalog_preserves_blank_and_soft_break_values(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'texts.th.txt';p.write_bytes('[language]\r\nไทย\r\n[hint]\r\nคนงาน\u200bว่าง\r\n[optional]\r\n\r\n'.encode())
            def windows_read_text(path,*args,encoding=None,**kwargs):
                with path.open('r',encoding=encoding or 'cp1252') as stream:return stream.read()
            with patch.object(Path,'read_text',autospec=True,side_effect=windows_read_text):
                self.assertEqual(capture.read_native(p),{'[language]':'ไทย','[hint]':'คนงาน\u200bว่าง','[optional]':''})
            p.write_text('[duplicate]\nx\n[duplicate]\ny\n')
            with self.assertRaisesRegex(ValueError,'Duplicate'):capture.read_native(p)
            p.write_text('[unpaired]\n')
            with self.assertRaisesRegex(ValueError,'unpaired'):capture.read_native(p)

    def test_phone_forces_valid_touch_boolean_and_desktop_clears_inherited_override(self):
        with patch.dict(os.environ,{'GLOB2_MOBILE_UI':'1','GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS':'1'}):
            desktop=capture.capture_environment(Path('profile'),1280)
            phone=capture.capture_environment(Path('profile'),390)
        self.assertNotIn('GLOB2_MOBILE_UI',desktop)
        self.assertEqual(phone['GLOB2_MOBILE_UI'],'1')
        self.assertNotIn('GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS',phone)
        self.assertEqual(phone['SDL_RENDER_DRIVER'],'software')

    def test_native_paths_with_spaces_are_not_shell_interpreted_and_failures_propagate(self):
        with tempfile.TemporaryDirectory(prefix='native capture ') as temp:
            root=Path(temp);command=[str(root/'Preview Harness.exe'),'screen',str(root),'input folder','output folder','390','844','settings']
            with patch.object(capture.subprocess,'run',side_effect=capture.subprocess.CalledProcessError(2,command)) as run:
                with self.assertRaises(capture.subprocess.CalledProcessError):capture.execute_capture(command,root,{'SDL_VIDEODRIVER':'dummy'},root/'capture.log')
                self.assertEqual(run.call_args.args[0],command)
                self.assertNotIn('shell',run.call_args.kwargs)
                self.assertTrue(run.call_args.kwargs['check'])
                self.assertEqual(run.call_args.kwargs['timeout'],180)

if __name__=='__main__':unittest.main()
