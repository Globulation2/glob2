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

    def test_usrmerge_loader_diversion_resolves_to_real_package_owner(self):
        from unittest.mock import patch
        def command(args,**kwargs):
            if args[0]=='ldd': return '/lib64/ld-linux-x86-64.so.2 (0x123)'
            if args[-1]=='/lib64/ld-linux-x86-64.so.2':
                return 'diversion by libc6 from: /lib64/ld-linux-x86-64.so.2\ndiversion by libc6 to: /lib64/ld-linux-x86-64.so.2.usr-is-merged\n'
            return 'libc6:amd64: /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'
        with patch.object(m.Path,'resolve',return_value=m.Path('/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2')):
            self.assertEqual(m.packages_for('binary',command),{'libc6:amd64'})

    def test_diversion_metadata_with_real_owner_is_not_ambiguous(self):
        def command(args,**kwargs):
            if args[0]=='ldd': return '/lib64/ld-linux-x86-64.so.2 (0x123)'
            return 'diversion by libc6 from: /lib64/ld-linux-x86-64.so.2\nlibc6:amd64: /lib64/ld-linux-x86-64.so.2'
        self.assertEqual(m.packages_for('binary',command),{'libc6:amd64'})

    def test_multiple_real_owners_remain_an_error(self):
        def command(args,**kwargs):
            if args[0]=='ldd': return '/lib64/ld-linux-x86-64.so.2 (0x123)'
            return 'libc6:amd64: /lib64/ld-linux-x86-64.so.2\nother-libc:amd64: /lib64/ld-linux-x86-64.so.2'
        with self.assertRaises(ValueError): m.packages_for('binary',command)
