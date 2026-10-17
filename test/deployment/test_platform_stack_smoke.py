"""Build every smoke-test image with one resolved design-system revision."""
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from platform_stack_smoke import Smoke


class SmokeBuildTests(unittest.TestCase):
    def test_build_receives_resolved_revision_before_compose(self):
        smoke = Smoke.__new__(Smoke)
        smoke.arguments = SimpleNamespace(attach=False, no_build=False)
        smoke.env = {}
        smoke.results = {}
        smoke.project = 'test'
        smoke.origin = 'https://localhost'
        revision = 'ab' * 20

        def compose(*args, **kwargs):
            self.assertEqual(smoke.env['GLOB2_DESIGN_SYSTEM_SHA'], revision)
            return 'test certificate'

        smoke.compose = Mock(side_effect=compose)
        with patch('platform_stack_smoke.subprocess.check_output', return_value=revision + '\n') as resolve, \
                patch('platform_stack_smoke.ssl.create_default_context'):
            smoke.up()
        resolve.assert_called_once()
        self.assertEqual(smoke.compose.call_args_list[0].args[0], 'build')


if __name__ == '__main__':
    unittest.main()
