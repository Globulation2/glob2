#!/usr/bin/env python3
"""Portable proof parsing and independently specified shortest-path cases."""
import struct
import tempfile
import unittest
from pathlib import Path
from gradient_proof import analyze, oracle, read_proof


class GradientProofTest(unittest.TestCase):
    def test_source_costs_and_wrapping(self):
        self.assertEqual(oracle(4, 1, 20, [65535, 1, 1, 1],
                                [(3, 9), (5, 9), (7, 9), (11, 9)]),
                         [65535, 65532, 65527, 65532])

    def test_deferred_sources_cap_and_obstacles(self):
        self.assertEqual(oracle(5, 1, 4, [65535, 1, 0, 65531, 1], [(3, 9)] * 5),
                         [65535, 65532, 0, 65531, 65532])
        self.assertEqual(oracle(5, 1, 20, [65535, 0, 1, 0, 0], [(3, 9)] * 5),
                         [65535, 0, 1, 0, 0])

    def test_proof_preserves_wide_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'proof.bin'
            payload = struct.pack('<5I2H4I', 0x47505246, 1, 2, 1, 100,
                                  65535, 1, 65535, 65536, 917514, 917514)
            path.write_bytes(payload)
            row = analyze(path)
            self.assertEqual(row['wrong_cells'], 1)
            self.assertEqual(row['out_of_range'], 1)
            self.assertEqual(row['examples'][0]['expected'], 65525)
            self.assertEqual(row['examples'][0]['actual'], 65536)
            for malformed in (payload[:19], payload[:-1], payload + b'\0',
                              struct.pack('<I', 0) + payload[4:]):
                path.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    read_proof(path)

    def test_transfer_proof_is_separate_from_field_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'transfer.bin'
            payload = struct.pack('<7I2H4I', 0x47545246, 1, 2, 2, 1, 100, 2,
                                  65535, 1, 917514, 917514, 917514, 1)
            path.write_bytes(payload)
            row = analyze(path)
            self.assertEqual(row['kind'], 'transfer')
            self.assertEqual(row['wrong_words'], 1)
            self.assertEqual(row['examples'], [dict(word=1, expected=917514, actual=1)])
            with self.assertRaises(ValueError):
                read_proof(path)
            for malformed in (payload[:27], payload[:-1], payload + b'\0'):
                path.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    analyze(path)


if __name__ == '__main__':
    unittest.main()
