"""tools/release/crash_gate.py: the crash-free promotion gate the release runbook runs."""

import io
import json
import os
import sys
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'release'))
import crash_gate  # noqa: E402


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def gate(verdict, **extra):
    return {
        'version': '0.11.8.4', 'verdict': verdict, 'reasons': ['because'], 'previousVersion': '0.11.8.3',
        'sessions': 2400, 'crashFreeSessions': 0.9931, 'previousSessions': 9000,
        'previousCrashFreeSessions': 0.9952, 'tolerance': 0.005, 'minimumSessions': 1000,
        'untriagedGroups': [], **extra,
    }


class CrashGateTests(unittest.TestCase):
    def setUp(self):
        env = mock.patch.dict(os.environ, {'CRASH_SYMBOL_UPLOAD_TOKEN': 'secret'})
        env.start()
        self.addCleanup(env.stop)
        self.requests = []

    def opener(self, *answers):
        answers = list(answers)

        def open_(request, timeout):
            self.requests.append(request)
            answer = answers.pop(0)
            if isinstance(answer, int):
                raise urllib.error.HTTPError(request.full_url, answer, 'no', {}, io.BytesIO(b'{"code":"x"}'))
            return Response(json.dumps(answer).encode())
        return open_

    def run_main(self, *argv, answers):
        stdout, stderr = io.StringIO(), io.StringIO()
        sleeps = []
        with mock.patch('sys.stdout', stdout), mock.patch('sys.stderr', stderr):
            code = crash_gate.main(['--version', '0.11.8.4', '--origin', 'https://glob2.test/', *argv],
                                   opener=self.opener(*answers), sleep=sleeps.append)
        return code, stdout.getvalue(), stderr.getvalue(), sleeps

    def test_pass_asks_with_the_token_and_exits_zero(self):
        code, out, _, _ = self.run_main(answers=[gate('pass')])
        self.assertEqual(code, 0)
        request = self.requests[0]
        self.assertEqual(request.full_url, 'https://glob2.test/api/v1/crash-releases/0.11.8.4/gate')
        self.assertEqual(request.headers['Authorization'], 'Bearer secret')
        self.assertIn('0.11.8.4: pass', out)
        self.assertIn('crash-free sessions 99.31% over 2400 sessions', out)
        self.assertIn('previous 0.11.8.3: 99.52%', out)

    def test_fail_exits_non_zero_with_reasons(self):
        code, out, _, _ = self.run_main(answers=[gate('fail', reasons=['Untriaged new crash group'])])
        self.assertEqual(code, 1)
        self.assertIn('- Untriaged new crash group', out)

    def test_insufficient_data_is_not_a_pass_unless_allowed(self):
        self.assertEqual(self.run_main(answers=[gate('insufficient_data')])[0], 2)
        self.assertEqual(self.run_main('--allow-insufficient', answers=[gate('insufficient_data')])[0], 0)

    def test_retries_server_errors_but_not_refusals(self):
        code, _, _, sleeps = self.run_main(answers=[503, gate('pass')])
        self.assertEqual((code, sleeps), (0, [2]))
        code, _, err, _ = self.run_main(answers=[404])
        self.assertEqual(code, 3)
        self.assertIn('gate refused (404)', err)

    def test_json_prints_the_answer(self):
        code, out, _, _ = self.run_main('--json', answers=[gate('pass')])
        self.assertEqual((code, json.loads(out)['verdict']), (0, 'pass'))

    def test_needs_the_token_and_a_release_version(self):
        with mock.patch.dict(os.environ, {'CRASH_SYMBOL_UPLOAD_TOKEN': ''}):
            self.assertEqual(self.run_main(answers=[])[0], 3)
        with mock.patch('sys.stderr', io.StringIO()), self.assertRaises(SystemExit):
            crash_gate.main(['--version', '0.11.8-dev'])

    def test_an_unknown_verdict_is_an_error(self):
        self.assertEqual(self.run_main(answers=[gate('maybe')])[0], 3)


class PromotionWorkflowTests(unittest.TestCase):
    """The store promotions hold on the gate (docs/releases/downloads.md#crash-reports-before-promotion)."""

    WORKFLOWS = {
        'android-play-internal.yml': "inputs.track == 'production'",
        'ios-production.yml': "inputs.stage == 'submit' || inputs.stage == 'publish'",
        'mac-app-store-production.yml': "inputs.stage == 'submit' || inputs.stage == 'publish'",
    }

    def test_each_promotion_runs_the_gate_before_the_store(self):
        for name, condition in self.WORKFLOWS.items():
            with self.subTest(name):
                text = (ROOT / '.github/workflows' / name).read_text()
                gate = text.index('python3 release-driver/tools/release/crash_gate.py')
                self.assertIn(condition, text[text.rindex('- name: Hold the promotion on the crash-free gate', 0, gate):gate])
                self.assertIn('allow_insufficient_crash_data', text)
                self.assertIn('tools/release/crash_gate.py\n            buildsys/official_instance.py', text)
                self.assertIn('secrets.CRASH_SYMBOL_UPLOAD_TOKEN', text)
                store = 'Install pinned Play API preflight tools' if name.startswith('android') else \
                    'Perform only the explicitly selected owner production action'
                self.assertLess(gate, text.index(store))


if __name__ == '__main__':
    unittest.main()
