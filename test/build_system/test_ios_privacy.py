"""The upload gate inspects final executables, including embedded frameworks."""
import sys
from pathlib import Path
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'platforms/mobile'))
from ios_privacy import check_app


class PrivacyTests(unittest.TestCase):
    def test_checks_app_and_embedded_framework_without_scanning_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            (app / 'Glob2').write_bytes(bytes.fromhex('cffaedfe'))
            framework = app / 'Frameworks/Compute.framework/Compute'
            framework.parent.mkdir(parents=True)
            framework.write_bytes(bytes.fromhex('cafebabe'))
            (app / 'map.gz').write_bytes(b'game data')
            def run(command, **kwargs):
                return ' _OBJC_CLASS_$_AVAssetWriter' if command[0] == 'nm' else '/System/Library/Frameworks/AVFoundation.framework/AVFoundation'
            self.assertEqual(check_app(app, run), ['Frameworks/Compute.framework/Compute', 'Glob2'])

    def test_rejects_camera_and_bluetooth_in_any_binary(self):
        for output in (' _OBJC_CLASS_$_AVCaptureDevice', ' _OBJC_CLASS_$_CBCentralManager',
                       '/System/Library/Frameworks/CoreBluetooth.framework/CoreBluetooth'):
            with self.subTest(output=output), tempfile.TemporaryDirectory() as directory:
                app = Path(directory)
                (app / 'Glob2').write_bytes(bytes.fromhex('cffaedfe'))
                with self.assertRaisesRegex(ValueError, 'unused privacy APIs'):
                    check_app(app, lambda *args, **kwargs: output)

    def test_empty_app_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'No Mach-O'):
                check_app(directory)
