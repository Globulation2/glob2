"""Build every smoke-test image with one resolved design-system revision."""
from pathlib import Path
import tempfile
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
        smoke.compose_logged = Mock(side_effect=compose)
        with patch('platform_stack_smoke.subprocess.check_output', return_value=revision + '\n') as resolve, \
                patch('platform_stack_smoke.ssl.create_default_context'):
            smoke.up()
        resolve.assert_called_once()
        smoke.compose_logged.assert_called_once()
        self.assertEqual(smoke.compose_logged.call_args.args[0], 'build.log')
        self.assertIn('build', smoke.compose_logged.call_args.args)
        self.assertEqual(smoke.compose.call_args_list[0].args[0], 'up')



class ItchFramingTests(unittest.TestCase):
    """The itch entry may be framed only by what the host opts in to (default 'none')."""

    def ancestors(self, env_text):
        with tempfile.TemporaryDirectory() as tmp:
            smoke = Smoke.__new__(Smoke)
            smoke.env_file = Path(tmp, 'host.env')
            smoke.env_file.write_text(env_text)
            return smoke.itch_frame_ancestors()

    def test_defaults_to_no_framing(self):
        self.assertEqual(self.ancestors('POSTGRES_PASSWORD=x\n'), "'none'")
        self.assertEqual(self.ancestors('GLOB2_ITCH_FRAME_ANCESTORS=\n'), "'none'")

    def test_follows_the_hosts_opt_in_as_compose_reads_it(self):
        itch = 'https://genixpro.itch.io https://html.itch.zone'
        self.assertEqual(self.ancestors(f'GLOB2_ITCH_FRAME_ANCESTORS="{itch}"\n'), itch)
        self.assertEqual(self.ancestors(f'GLOB2_ITCH_FRAME_ANCESTORS={itch}\n'), itch)


if __name__ == '__main__':
    unittest.main()
