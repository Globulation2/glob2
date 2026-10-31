"""The Store upload gate must inspect results even when MakePkg exits zero."""
import importlib.util
import contextlib
import io
import hashlib
from unittest.mock import patch
from zipfile import ZipFile
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/windows_store_validator.py"
spec = importlib.util.spec_from_file_location("validator", SCRIPT)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

PASS = '''<XboxOneSubmissionValidator><engineversion>2610.309.15000.0</engineversion><ValidationSummary><TotalWarnings>0</TotalWarnings>
<TotalFailures>0</TotalFailures><Result>Pass</Result></ValidationSummary>
<testresults><testresult><component>Foreign Files Check</component><filecount>0</filecount>
</testresult></testresults><validatorrun><result>Pass</result></validatorrun></XboxOneSubmissionValidator>'''


class StoreValidatorTest(unittest.TestCase):
    def check(self, text):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "report.xml"
            report.write_text(text)
            validator.require_passing_report(report)

    def test_stage_validator_checks_digest_before_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'validator.zip'
            archive.write_bytes(b'not the reviewed Microsoft ZIP')
            with self.assertRaisesRegex(RuntimeError, 'digest mismatch'):
                validator.stage_validator(archive, root / 'staged')
            self.assertFalse((root / 'staged').exists())

    def test_validator_layout_rejects_traversal_and_only_writes_expected_files(self):
        for extra in (None, '../outside.dll'):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = root / 'validator.zip'
                with ZipFile(archive, 'w') as bundle:
                    for name in validator.VALIDATOR_FILES:
                        bundle.writestr(name, name.encode())
                    if extra:
                        bundle.writestr(extra, b'x')
                digest = hashlib.sha256(archive.read_bytes()).hexdigest()
                with patch.object(validator, 'VALIDATOR_SHA256', digest):
                    if extra:
                        with self.assertRaisesRegex(RuntimeError, 'layout'):
                            validator.stage_validator(archive, root / 'staged')
                        self.assertFalse((root / 'staged').exists())
                    else:
                        validator.stage_validator(archive, root / 'staged')
                        self.assertEqual({p.name for p in (root / 'staged').iterdir()}, validator.VALIDATOR_FILES)

    def test_passing_old_validator_is_not_accepted(self):
        with self.assertRaisesRegex(RuntimeError, 'version pin'):
            self.check(PASS.replace('2610.309.15000.0', '2610.307.18000.0'))

    def test_complete_passing_report(self):
        self.check(PASS)

    def test_real_update_failure_shape_is_rejected(self):
        failed = PASS.replace('<TotalFailures>0', '<TotalFailures>1').replace('>Pass<', '>Fail<')
        failed = failed.replace('</testresult>', '<failure Id="ILI_OnlineSubValUnsupportedGDK">Update required</failure></testresult>')
        with self.assertRaisesRegex(RuntimeError, 'ILI_OnlineSubValUnsupportedGDK'):
            self.check(failed)

    def test_cli_emits_counts_and_id_without_report_prose(self):
        failed = PASS.replace('<TotalFailures>0', '<TotalFailures>1').replace('>Pass<', '>Fail<')
        failed = failed.replace('</testresult>', '<failure Id="ILI_OnlineSubValUnsupportedGDK">Update required</failure></testresult>')
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'report.xml'
            report.write_text(failed)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(validator.check_cli(report), 1)
            self.assertIn('::error::', output.getvalue())
            self.assertIn('failures=1, warnings=0, IDs=ILI_OnlineSubValUnsupportedGDK', output.getvalue())
            self.assertNotIn('Update required', output.getvalue())
            self.assertNotIn(str(report), output.getvalue())

    def test_annotation_escapes_workflow_command_newlines_and_percent(self):
        self.assertEqual(validator.annotation_escape('a%\r\nb'), 'a%25%0D%0Ab')

    def test_missing_report_has_fixed_failure_annotation(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(validator.check_cli(Path(temporary) / 'missing.xml'), 1)
            self.assertEqual(output.getvalue(), '::error::Submission Validator XML report is missing or malformed.\n')

    def test_fail_closed_for_missing_incomplete_or_inconsistent_results(self):
        for text in ('<other/>', PASS.replace('<Result>Pass</Result>', ''),
                     PASS.replace('<TotalFailures>0', '<TotalFailures>1'),
                     PASS.replace('<TotalWarnings>0', '<TotalWarnings>invalid'),
                     PASS.replace('<result>Pass</result>', '<result>Fail</result>'),
                     PASS.replace('</testresult>', '<failure Id="hidden">Failure</failure></testresult>')):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                self.check(text)


if __name__ == '__main__':
    unittest.main()
