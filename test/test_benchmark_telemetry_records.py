"""Check bounded-memory sidecar comparisons against the original byte oracle."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

from check_telemetry_simulation import detailed_tick_hashes, detailed_ticks


def fixture(fields=3):
    data = bytearray(b'GCS1' + struct.pack('<4I', 2, 7, 3, 11))
    for tick in (19, 20, 27):
        data.extend(struct.pack('<2I', tick, tick + 100))
        for team in range(2):
            data.extend(struct.pack('<I', tick + team))
            for entities in ((team + tick) % 3, team):
                data.extend(struct.pack('<I', entities))
                for entity in range(entities):
                    count = fields if entity == 0 else 0
                    data.extend(struct.pack('<IHI', entity, team, count))
                    data.extend(struct.pack('<I', tick + team + entity) * count)
    return bytes(data)


class TelemetryRecordsTest(unittest.TestCase):
    def hash_records(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'records'
            path.write_bytes(data)
            return detailed_tick_hashes(path)

    def test_matches_complete_byte_oracle(self):
        for fields in (0, 3, 17000):  # Also cross the bounded read boundary.
            data = fixture(fields)
            self.assertEqual(self.hash_records(data), {
                tick: hashlib.sha256(payload).digest()
                for tick, payload in detailed_ticks(data).items()})

    def test_excludes_only_metadata_and_aggregate(self):
        data = fixture()
        expected = self.hash_records(data)
        changed = bytearray(data)
        # Retained header metadata and first tick's aggregate are excluded.
        for offset in (8, 16, 24):
            changed[offset] ^= 1
        self.assertEqual(self.hash_records(changed), expected)
        # Team checksum, entity header and field payload are all included.
        for offset in (28, 36, 46):
            changed = bytearray(data)
            changed[offset] ^= 1
            actual = self.hash_records(changed)
            self.assertNotEqual(actual[19], expected[19])
            self.assertEqual(actual[20], expected[20])
            self.assertEqual(actual[27], expected[27])

    def test_rejects_every_truncation_and_extra_data(self):
        data = fixture()
        for size in range(len(data)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                self.hash_records(data[:size])
        with self.assertRaises(ValueError):
            self.hash_records(data + b'\0')
        with self.assertRaises(ValueError):
            self.hash_records(b'BAD!' + data[4:])

    def test_rejects_duplicate_ticks(self):
        data = b'GCS1' + struct.pack('<4I', 0, 0, 2, 0)
        data += struct.pack('<2I', 19, 0) * 2
        with self.assertRaises(ValueError):
            self.hash_records(data)


if __name__ == '__main__':
    unittest.main()
