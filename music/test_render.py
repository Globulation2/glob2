"""Timing tests for the MIDI-to-offline-render bridge."""
import struct
import tempfile
import unittest
from pathlib import Path
from midi_tools import vlq
from render import read_midi


def track(events):
    payload = b''.join(vlq(delta)+event for delta,event in events)
    return b'MTrk'+struct.pack('>I',len(payload))+payload


class MIDIClockTest(unittest.TestCase):
    def read(self, tracks):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'timing.mid'
            path.write_bytes(b'MThd'+struct.pack('>IHHH',6,1,len(tracks),480)+b''.join(tracks))
            return read_midi(path)

    def test_shared_tempo_map_and_running_status(self):
        conductor = track([(0,b'\xff\x51\x03\x07\xa1\x20'),
                           (480,b'\xff\x51\x03\x0f\x42\x40'),(480,b'\xff\x2f\x00')])
        notes = track([(0,b'\xc0\x0c'),(0,b'\x90\x3c\x60'),
                       (480,b'\x3c\x00'),(0,b'\x40\x60'),
                       (480,b'\x40\x00'),(0,b'\xff\x2f\x00')])
        frames, events = self.read([conductor,notes])
        self.assertEqual(frames,66150)
        self.assertEqual([e['frame'] for e in events],[0,0,22050,22050,66150])
        self.assertEqual(events[-1]['status'],0x90)
        self.assertEqual(events[-1]['data2'],0)

    def test_drum_channel_program_and_silent_tail(self):
        frames, events = self.read([track([(0,b'\xc9\x00'),(0,b'\x99\x24\x64'),
                                           (120,b'\x89\x24\x00'),(840,b'\xff\x2f\x00')])])
        self.assertEqual(frames,44100)
        self.assertEqual(events[0]['status'],0xc9)
        self.assertEqual(events[1]['status'],0x99)
        self.assertEqual(events[2]['frame'],5512)

    def test_reject_mid_song_program_changes(self):
        with self.assertRaisesRegex(ValueError,'tick zero'):
            self.read([track([(10,b'\xc0\x0c'),(470,b'\xff\x2f\x00')])])


class FluidPlaybackTest(unittest.TestCase):
    def test_expired_one_shot_and_repeat_render(self):
        from fluid_renderer import FluidRenderer
        import numpy as np
        bank = Path(__file__).resolve().parent/'soundfonts/GeneralUser-GS.sf2'
        if not bank.exists(): self.skipTest('Sound bank not downloaded')
        try: renderer = FluidRenderer()
        except (RuntimeError, OSError) as error: self.skipTest(str(error))
        events = [dict(frame=0,status=0xc9,data1=0,data2=0),
                  dict(frame=0,status=0x99,data1=42,data2=100),
                  dict(frame=20000,status=0x89,data1=42,data2=0)]
        first = renderer.render(22050,events,bank)
        second = renderer.render(22050,events,bank)
        self.assertTrue(np.isfinite(first).all())
        self.assertGreater(float(np.max(np.abs(first))),0)
        self.assertTrue(np.array_equal(first,second))


if __name__ == '__main__': unittest.main()
