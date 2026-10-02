import importlib.util
from pathlib import Path
import subprocess
import unittest
spec=importlib.util.spec_from_file_location('runtime_packages',Path(__file__).resolve().parents[2]/'.github/scripts/linux_runtime_packages.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class RuntimePackagesTest(unittest.TestCase):
    def test_real_dependencies_use_package_owners_and_preserve_architecture(self):
        def command(args, **kwargs):
            return 'libSDL2 => /lib/x86_64-linux-gnu/libSDL2.so (0x1)\n/lib64/ld-linux.so (0x2)' if args[0]=='ldd' else 'libsdl2-2.0-0:amd64: '+args[-1]
        self.assertEqual(m.packages_for('binary',command),{'libsdl2-2.0-0:amd64'})
    def test_missing_libraries_are_errors(self):
        with self.assertRaisesRegex(ValueError,'unresolved'):
            m.packages_for('binary',lambda *a,**k:'libmissing.so => not found')
    def test_unowned_system_library_is_not_silently_omitted(self):
        def command(args,**kwargs):
            if args[0]=='ldd': return '/lib/unknown.so (0x1)'
            raise subprocess.CalledProcessError(1,args)
        with self.assertRaisesRegex(ValueError,'no runtime package'):
            m.packages_for('binary',command)
