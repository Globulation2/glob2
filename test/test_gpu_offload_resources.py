import subprocess
import unittest
from unittest.mock import patch
from benchmark_gpu_offload import gpu_snapshot


class GPUResourcesTest(unittest.TestCase):
    def query(self, processes):
        devices = '0, GPU-selected, "Device, zero", 580, 1, 2, 100, 8192, 40, 30, 1500, 7000\n'
        devices += '1, GPU-other, Other, 580, 90, 90, 2000, 8192, 70, 200, 1700, 7000\n'
        return [subprocess.CompletedProcess([], 0, devices, ''),
                subprocess.CompletedProcess([], 0, processes, '')]

    @patch('benchmark_gpu_offload.shutil.which', return_value='/usr/bin/nvidia-smi')
    def test_other_gpu_compute_is_retained_without_selected_device_contamination(self, _):
        with patch('benchmark_gpu_offload.subprocess.run', side_effect=self.query('GPU-other, 1234, 2000\n')):
            result = gpu_snapshot('GPU-selected')
        self.assertTrue(result['available'])
        self.assertTrue(result['selected_device_found'])
        self.assertEqual(result['selected_compute_processes'], [])
        self.assertEqual(result['compute_processes'][0]['pid'], '1234')
        self.assertEqual(result['devices'][0]['name'], 'Device, zero')

    @patch('benchmark_gpu_offload.shutil.which', return_value='/usr/bin/nvidia-smi')
    def test_selected_device_compute_and_missing_identity_are_explicit(self, _):
        with patch('benchmark_gpu_offload.subprocess.run', side_effect=self.query('GPU-selected, 55, 100\n')):
            result = gpu_snapshot('GPU-selected')
        self.assertEqual(result['selected_compute_processes'][0]['pid'], '55')
        with patch('benchmark_gpu_offload.subprocess.run', side_effect=self.query('')):
            result = gpu_snapshot('GPU-absent')
        self.assertFalse(result['selected_device_found'])

    @patch('benchmark_gpu_offload.shutil.which', return_value='/usr/bin/nvidia-smi')
    def test_malformed_observation_cannot_establish_availability(self, _):
        with patch('benchmark_gpu_offload.subprocess.run', return_value=subprocess.CompletedProcess([], 0, 'bad,row', '')):
            result = gpu_snapshot('GPU-selected')
        self.assertFalse(result['available'])
        self.assertIn('malformed', result['error'])


if __name__ == '__main__':
    unittest.main()
