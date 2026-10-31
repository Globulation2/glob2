"""The Store upload gate must inspect results even when MakePkg exits zero."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/windows_store_validator.py"
spec = importlib.util.spec_from_file_location("validator", SCRIPT)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

PASS = '''<XboxOneSubmissionValidator><ValidationSummary><TotalWarnings>0</TotalWarnings>
<TotalFailures>0</TotalFailures><Result>Pass</Result></ValidationSummary>
<testresults><testresult><component>Foreign Files Check</component><filecount>0</filecount>
</testresult></testresults><validatorrun><result>Pass</result></validatorrun></XboxOneSubmissionValidator>'''


class StoreValidatorTest(unittest.TestCase):
    def check(self, text):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "report.xml"
            report.write_text(text)
            validator.require_passing_report(report)

    def test_complete_passing_report(self):
        self.check(PASS)

    def test_real_update_failure_shape_is_rejected(self):
        failed = PASS.replace('<TotalFailures>0', '<TotalFailures>1').replace('>Pass<', '>Fail<')
        failed = failed.replace('</testresult>', '<failure Id="ILI_OnlineSubValUnsupportedGDK">Update required</failure></testresult>')
        with self.assertRaisesRegex(RuntimeError, 'ILI_OnlineSubValUnsupportedGDK'):
            self.check(failed)

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
