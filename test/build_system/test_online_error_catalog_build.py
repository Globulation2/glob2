"""Execute actual builder wiring against SCons DAG nodes, without SDKs/compilation."""
import ast
import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import types
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
from SCons.Script import Environment
from online_error_catalog import configure, bind_objects, bind_consumer, _generate
from dev_compile import unique


def tree(path):
    return ast.parse((ROOT / path).read_text(), filename=path)


def function(path, name):
    return next(node for node in ast.walk(tree(path))
                if isinstance(node, ast.FunctionDef) and node.name == name)


def execute(nodes, scope, filename):
    program = ast.fix_missing_locations(ast.Module(body=copy.deepcopy(nodes), type_ignores=[]))
    exec(compile(program, filename, 'exec'), scope)


# Keep the actual configure/bind calls and their branch conditions. Other builder
# work would discover/download SDKs or invoke compiler toolchains; exclude it.
# A missing helper call, wrong object mapping, wrong branch or wrong variant
# output therefore changes the executable DAG assertions below.
def contract_nodes(nodes):
    result = []
    for node in nodes:
        if isinstance(node, (ast.Assign, ast.Expr)):
            call = node.value
            if isinstance(call, ast.Call) and isinstance(call.func, ast.Name) and call.func.id in (
                    'configure_online_error_catalog', 'bind_online_error_objects'):
                result.append(node)
        elif isinstance(node, ast.If):
            body, otherwise = contract_nodes(node.body), contract_nodes(node.orelse)
            if body or otherwise:
                result.append(ast.If(test=node.test, body=body or [ast.Pass()], orelse=otherwise))
    return result


class OnlineErrorBuildTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)

    def env(self):
        # Builder registration only. No SCons task or compiler is executed.
        return Environment(tools=['g++'])

    def graph(self, builder, target):
        env = self.env()
        output = self.output / target
        consumer = env.File(str(output / 'api.o'))
        unrelated = env.File(str(output / 'other.o'))
        mapping = {'src/online/ApiErrorText.cpp': consumer, 'src/other.cpp': unrelated}
        env.Append(CPPPATH=[str(output / 'include')])
        before_includes = list(env['CPPPATH'])
        env['BUILDDIR'] = str(output)
        scope = dict(env=env, local=env, output=output, Path=Path, identity={'target': target},
                     online_error_catalog_include=str(output),
                     Dir=lambda _: env.Dir(str(ROOT)),
                     configure_online_error_catalog=configure,
                     bind_online_error_objects=bind_objects,
                     by_source=mapping,
                     ordinary_mapping={'online/ApiErrorText.cpp': consumer, 'other.cpp': unrelated})
        if builder == 'src/SConscript':
            nodes = tree(builder).body
        else:
            nodes = function(builder, '_build_variant' if target.startswith('web') else 'build_mobile').body
        executable = contract_nodes(nodes)
        self.assertTrue(executable)
        execute(executable, scope, builder)
        catalog = scope['online_error_catalog']
        self.assertEqual(len(catalog), 1)
        header = catalog[0]
        self.assertFalse(Path(header.abspath).exists(), 'Test must begin without a generated include')
        self.assertTrue(header.has_builder())
        self.assertEqual(Path(header.abspath), output / 'include/generated/OnlineErrorCatalog.h')
        self.assertEqual(before_includes, env['CPPPATH'], 'No new compile signature/global include path')
        self.assertEqual(before_includes, env.Clone()['CPPPATH'])
        self.assertIn(header, consumer.depends)
        self.assertNotIn(header, unrelated.depends)
        sources = [source.abspath.replace('\\', '/') for source in header.sources]
        for suffix in ('tools/localization/build_online_error_catalog.py',
                       'services/online/packages/i18n/locales/en.json', 'services/online/packages/i18n/locales/bg.json'):
            self.assertTrue(any(name.endswith('/' + suffix) for name in sources), suffix)
        return env, catalog, mapping

    def test_native_and_each_independent_web_mobile_builder_register_missing_header(self):
        paths = []
        for builder, target in [('src/SConscript', 'native'), ('scons/web_build.py', 'web-serial'),
                                ('scons/web_build.py', 'web-threaded'),
                                ('scons/mobile_build.py', 'android'), ('scons/mobile_build.py', 'ios')]:
            with self.subTest(builder=builder, target=target):
                _, header, _ = self.graph(builder, target)
                paths.append(header[0].abspath)
        self.assertEqual(len(paths), len(set(paths)), 'Variants must not share generated output')

    def native_functions(self, env, catalog):
        scope = dict(test_env=env, online_error_catalog=catalog,
                     online_error_catalog_include=str(Path(catalog[0].abspath).parents[1]),
                     available=lambda options: True,
                     source_node=lambda source: env.File(str(ROOT / source.lstrip('#'))),
                     bind_online_error_consumer=bind_consumer)
        execute([function('test/SConscript', 'objects')], scope, 'test/SConscript')
        original = scope['objects']
        scope['objects'] = lambda entries, prefix: original(entries, str(self.output / prefix))
        execute([function('test/SConscript', 'production_objects')], scope, 'test/SConscript')
        return scope

    def test_native_unit_tests_and_production_reuse_and_fallback_depend_on_header(self):
        env, catalog, mapping = self.graph('src/SConscript', 'native')
        scope = self.native_functions(env.Clone(), catalog)
        test_object = scope['objects'](['#src/online/ApiErrorTextTest.cpp'], 'test-')[0]
        self.assertIn(catalog[0], test_object.depends)
        scope['client_object_by_source'] = {'online/ApiErrorText.cpp': mapping['src/online/ApiErrorText.cpp']}
        reused = scope['production_objects'](['#src/online/ApiErrorText.cpp'])[0]
        self.assertEqual(reused, mapping['src/online/ApiErrorText.cpp'])
        scope['client_object_by_source'] = {}
        fallback = scope['production_objects']([('#src/online/ApiErrorText.cpp', {'defines': ['FIXTURE']})])[0]
        self.assertIn(catalog[0], fallback.depends)
        self.assertIn(str(Path(catalog[0].abspath).parents[1]), scope['test_env']['CPPPATH'])

    def android_functions(self, env, catalog, mapping):
        registry = types.SimpleNamespace(source_path=lambda source: source.lstrip('#'))
        scope = dict(tests=env, object_root=self.output / 'android-objects',
                     registry=registry, by_source=mapping, unique=unique, available=lambda options: True,
                     online_error_catalog=catalog, bind_online_error_consumer=bind_consumer)
        execute([function('scons/mobile_build.py', 'test_objects'),
                 function('scons/mobile_build.py', 'production_objects')], scope, 'scons/mobile_build.py')
        return scope

    def test_android_unit_tests_and_production_reuse_and_fallback_depend_on_header(self):
        env, catalog, mapping = self.graph('scons/mobile_build.py', 'android')
        scope = self.android_functions(env.Clone(), catalog, mapping)
        test_object = scope['test_objects'](['#src/online/ApiErrorTextTest.cpp'], 'test-')[0]
        self.assertIn(catalog[0], test_object.depends)
        reused = scope['production_objects'](['#src/online/ApiErrorText.cpp'])[0]
        self.assertEqual(reused, mapping['src/online/ApiErrorText.cpp'])
        fallback = scope['production_objects']([('#src/online/ApiErrorText.cpp', {'defines': ['FIXTURE']})])[0]
        self.assertIn(catalog[0], fallback.depends)

    def test_web_and_ios_actual_test_object_loops_attach_header(self):
        for builder, target in [('scons/web_build.py', 'web-serial'), ('scons/web_build.py', 'web-threaded'),
                                ('scons/mobile_build.py', 'ios')]:
            with self.subTest(target=target):
                env, catalog, _ = self.graph(builder, target)
                outer = function(builder, '_build_variant' if target.startswith('web') else 'build_mobile')
                loop = next(node for node in ast.walk(outer) if isinstance(node, ast.For)
                            and any(isinstance(child, ast.Call) and isinstance(child.func, ast.Name)
                                    and child.func.id == 'bind_online_error_consumer' for child in ast.walk(node)))
                registry = types.SimpleNamespace(SUPPORT=[], ENGINE_SUPPORT=[], scripting_entries=lambda: [],
                                                 source_path=lambda source: source.lstrip('#'))
                scope = dict(tests=env.Clone(), test_objects=[], object_root=self.output / (target + '-objects'),
                             output=self.output / (target + '-tests'), registry=registry,
                             archive_object_name=lambda source: source.replace('/', '_') + '.o',
                             online_error_catalog=catalog, bind_online_error_consumer=bind_consumer)
                execute([loop], scope, builder)
                affected = [node for node in scope['test_objects'] if 'ApiErrorTextTest' in str(node)]
                self.assertEqual(len(affected), 1)
                self.assertIn(catalog[0], affected[0].depends)

    def test_registry_links_presenter_in_real_unit_production_inventory(self):
        spec = importlib.util.spec_from_file_location('error_build_registry', ROOT / 'test/tests.py')
        registry = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(registry)
        paths = lambda entries: {entry[0] if isinstance(entry, tuple) else entry for entry in entries}
        self.assertIn('#src/online/ApiErrorTextTest.cpp', paths(registry.UNIT_TESTS))
        self.assertIn('#src/online/ApiErrorText.cpp', paths(registry.UNIT_PRODUCTION_SOURCES))

    def test_real_generation_action_writes_the_declared_missing_include(self):
        env = self.env()
        catalog = configure(env, self.output / 'action/include')
        source = [env.File(str(ROOT / name)) for name in ('tools/localization/build_online_error_catalog.py',
                  'services/online/packages/i18n/locales/en.json', 'services/online/packages/i18n/locales/bg.json')]
        self.assertFalse(Path(catalog[0].abspath).exists())
        self.assertEqual(_generate(catalog, source, env), 0)
        self.assertTrue(Path(catalog[0].abspath).read_bytes().isascii())

if __name__ == '__main__': unittest.main()
