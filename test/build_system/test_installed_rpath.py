"""Installed clients must not load development SDL runtimes by ELF RUNPATH."""
import ast
from pathlib import Path
import unittest


class InstalledRpathTests(unittest.TestCase):
    def filter(self, env, targets):
        # Execute the actual install filter without configuring the whole SDK or
        # rewriting the active build's generated headers.
        tree = ast.parse((Path(__file__).resolve().parents[2] / 'SConstruct').read_text())
        blocks = [node for node in ast.walk(tree) if isinstance(node, ast.If)
                  and ast.unparse(node.test) == "sdl_prefix and 'install' in COMMAND_LINE_TARGETS"]
        self.assertEqual(len(blocks), 1)
        code = compile(ast.Module(body=blocks, type_ignores=[]), 'SConstruct install RPATH', 'exec')
        exec(code, {'env': env, 'sdl_prefix': '/sdk/pinned', 'COMMAND_LINE_TARGETS': targets})
        return env

    def test_removes_both_pkg_config_forms_and_scons_rpath(self):
        env = {'RPATH': ['/sdk/pinned/lib', '$ORIGIN/../lib/glob2', '/system/lib'],
               'LINKFLAGS': ['-Wl,-rpath,/sdk/pinned/lib', '-Wl,-rpath=/sdk/pinned/lib',
                             '-Wl,-rpath,$ORIGIN/../lib/glob2', '-Wl,-rpath=/system/lib',
                             '-L/sdk/pinned/lib', '-Wl,-rpath=/sdk/pinned/lib/extensions']}
        self.filter(env, ['install'])
        self.assertEqual(env['RPATH'], ['$ORIGIN/../lib/glob2', '/system/lib'])
        self.assertEqual(env['LINKFLAGS'], ['-Wl,-rpath,$ORIGIN/../lib/glob2',
            '-Wl,-rpath=/system/lib', '-L/sdk/pinned/lib', '-Wl,-rpath=/sdk/pinned/lib/extensions'])

    def test_development_keeps_prefix_runtime(self):
        env = {'RPATH': ['/sdk/pinned/lib'], 'LINKFLAGS': ['-Wl,-rpath=/sdk/pinned/lib']}
        self.filter(env, ['glob2'])
        self.assertEqual(env, {'RPATH': ['/sdk/pinned/lib'],
                              'LINKFLAGS': ['-Wl,-rpath=/sdk/pinned/lib']})


if __name__ == '__main__':
    unittest.main()
