"""Portable Store adaptation must preserve released payload bytes and fail closed."""
import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/windows_store_portable.py"
spec = importlib.util.spec_from_file_location("portable", SCRIPT)
portable = importlib.util.module_from_spec(spec)
spec.loader.exec_module(portable)


class PortableStoreTest(unittest.TestCase):
    def archive(self, root, extra=None):
        payload = {"glob2.exe": b"MZimmutable released binary", "COPYING": b"GPL\n",
                   "data/icons/glob2-icon-128x128.webp": b"released icon",
                   "SDL3.dll": b"MZimmutable released DLL",
                   "data/map.webp": b"immutable map",
                   "licenses/binutils/COPYING.LIB": b"License\r\n"}
        payload.update(extra or {})
        archive = root / "payload.zip"
        with ZipFile(archive, "w") as bundle:
            for name, data in payload.items():
                bundle.writestr("Globulation2/" + name, data)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        return archive, digest, payload

    def test_every_payload_byte_preserved_except_notice_filename(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive, digest, payload = self.archive(root)
            target = root / "staged"
            portable.stage_archive(archive, target, digest)
            for name, data in payload.items():
                expected = name + ".txt" if name.endswith(".LIB") else name
                self.assertEqual((target / expected).read_bytes(), data)
            self.assertEqual(len(list(p for p in target.rglob("*") if p.is_file())), len(payload))

    def test_digest_and_unsafe_members_leave_existing_output_unchanged(self):
        for extra in ({}, {"../escape": b"x"}, {"glob2.EXE": b"collision"},
                      {"licenses/binutils/COPYING.LIB.txt": b"collision"},
                      {"licenses/binutils/COPYING.LIB": b"MZbinary"}):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive, digest, _ = self.archive(root, extra)
                target = root / "staged"
                target.mkdir()
                sentinel = target / "existing"
                sentinel.write_bytes(b"preserve")
                with self.assertRaises(RuntimeError):
                    portable.stage_archive(archive, target, digest if extra else "0" * 64)
                self.assertEqual(sentinel.read_bytes(), b"preserve")


if __name__ == "__main__":
    unittest.main()
