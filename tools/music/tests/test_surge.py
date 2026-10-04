# SPDX-License-Identifier: GPL-3.0-or-later
"""Surge XT backend: state (de)serialisation, patch XML editing (including the filter
enable fix), .fxp export and the determinism rules, without network. The render tests
run only when pedalboard and an already-cached Surge XT are available."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from glob2music.backends import surge  # noqa: E402
from glob2music.spec import SAMPLE_RATE as SR  # noqa: E402

INIT_XML = ('<?xml version="1.0" encoding="UTF-8"?><patch revision="24"><meta name="Init" category="Init" />'
            '<parameters><a_filter1_type type="0" value="0" deactivated="1" />'
            '<a_filter1_cutoff type="2" value="3.0" />'
            '<a_filter2_type type="0" value="0" deactivated="1" />'
            '<a_ws_type type="0" value="0" deactivated="1" />'
            '<a_filter1_envmod type="2" value="0.0" /><a_pitch type="2" value="0.0" /></parameters></patch>')


def component(xml=INIT_XML, tail=b'\0' * 16 + b'JUCEPrivateData'):
    body = xml.encode()
    return b'sub3' + struct.pack('<I', len(body)) + b'\0' * 24 + body + tail


def vst3_state(comp):
    xml = f'<?xml version="1.0"?> <VST3PluginState><IComponent>{surge.juce_b64encode(comp)}</IComponent></VST3PluginState>'
    body = xml.encode() + b'\0'
    return b'VC2!' + struct.pack('<I', len(body)) + body


class StateBlobTest(unittest.TestCase):
    def test_juce_base64_matches_known_vector_and_round_trips(self):
        self.assertEqual(surge.juce_b64encode(b'\x01'), '1.A.')      # LSB-first 6-bit groups
        self.assertEqual(surge.juce_b64decode('1.A.'), b'\x01')
        data = bytes(np.random.default_rng(0).integers(0, 256, 1001, dtype=np.uint8))
        self.assertEqual(surge.juce_b64decode(surge.juce_b64encode(data)), data)

    def test_component_wraps_into_vst3_state_and_back(self):
        comp = component()
        raw = vst3_state(comp)
        self.assertEqual(surge.unwrap_state(raw), comp)
        other = component(INIT_XML.replace('Init', 'Other'))
        self.assertEqual(surge.unwrap_state(surge.wrap_state(raw, other)), other)

    def test_join_component_rewrites_the_xml_size(self):
        head, xml, tail = surge.split_component(component())
        joined = surge.join_component(head, xml + '<!-- longer -->', tail)
        self.assertEqual(surge.split_component(joined), (joined[:32], xml + '<!-- longer -->', tail))


class PatchXmlTest(unittest.TestCase):
    def edit(self, **kw):
        patch = surge.SurgePatch('t', **kw)
        return surge.edit_patch_xml(INIT_XML, patch)

    def test_filter_enable_fix_activates_selected_stages_only(self):
        xml = self.edit(params={'a_filter_1_type': 'LP 24 dB', 'a_waveshaper_type': 'Soft'})
        self.assertIn('<a_filter1_type type="0" value="0" deactivated="0"', xml)
        self.assertIn('<a_ws_type type="0" value="0" deactivated="0"', xml)
        self.assertIn('<a_filter2_type type="0" value="0" deactivated="1"', xml)   # not used: untouched
        xml = self.edit(params={'a_filter_1_type': 'Off'})
        self.assertIn('<a_filter1_type type="0" value="0" deactivated="1"', xml)

    def test_xml_values_mod_routings_and_name(self):
        xml = self.edit(params={}, xml={'a_filter1_envmod': 8.0},
                        mods=[('a_pitch', surge.SRC_LFO1, 0.18), ('a_pitch', surge.SRC_VELOCITY, 0.5)])
        self.assertIn('<meta name="t"', xml)
        self.assertIn('<a_filter1_envmod type="2" value="8.000000" />', xml)
        self.assertRegex(xml, r'<a_pitch [^>]*><modrouting source="17" depth="0.180000"[^>]*/>'
                              r'<modrouting source="1" depth="0.500000"[^>]*/></a_pitch>')

    def test_unknown_targets_raise(self):
        with self.assertRaises(KeyError):
            self.edit(params={}, xml={'a_nope': 1.0})
        with self.assertRaises(KeyError):
            self.edit(params={}, mods=[('a_nope', 1, 1.0)])


class HelpersTest(unittest.TestCase):
    def test_fxp_is_an_opaque_chunk_program_without_private_data(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'x.fxp'
            surge.write_fxp(component(), path, 'glass')
            data = path.read_bytes()
        self.assertEqual(data[:4], b'CcnK')
        self.assertEqual(data[8:12], b'FPCh')
        self.assertEqual(struct.unpack('>i', data[4:8])[0], len(data) - 8)
        self.assertEqual(data[28:33], b'glass')
        self.assertNotIn(b'JUCEPrivateData', data)
        self.assertTrue(data.endswith(b'</patch>'))

    def test_display_numbers_are_parsed_in_base_units(self):
        self.assertEqual(surge._number('2.50 s'), 2500.0)
        self.assertEqual(surge._number('902.5 ms'), 902.5)
        self.assertIsNone(surge._number('Sine'))
        self.assertTrue(surge._matches('901.09', 900.0))
        self.assertFalse(surge._matches('0.00 semitones', 9.0))

    def test_determinism_rules_are_enforced(self):
        good = surge.SurgePatch('ok', params={**surge.DETERMINISTIC, 'a_lfo_1_trigger_mode': 'Keytrigger'})
        surge.check_deterministic(good)
        for params in ({'a_lfo_1_trigger_mode': 'Keytrigger'},
                       {**surge.DETERMINISTIC, 'a_lfo_1_trigger_mode': 'Freerun'},
                       {**surge.DETERMINISTIC, 'a_osc_drift': 20.0},
                       {**surge.DETERMINISTIC, 'a_lfo_2_trigger_mode': 'Random'}):
            with self.assertRaises(ValueError):
                surge.check_deterministic(surge.SurgePatch('bad', params=params))


def _cached_surge():
    try:
        import pedalboard  # noqa: F401
        return surge.surge_vst3(offline=True)
    except Exception:
        return None


@unittest.skipUnless(_cached_surge(), 'needs pedalboard and a cached Surge XT (python3 -m glob2music build glass-garden)')
class SurgeRenderTest(unittest.TestCase):
    """Regression test for the filter enable fix: a saw through a 300 Hz 24 dB low-pass
    must have (almost) nothing above 6 kHz; unfiltered it has about -19 dB there."""

    def test_filter_is_active(self):
        host = surge.SurgeHost(_cached_surge())
        host.load(surge.SurgePatch('saw', params={'a_filter_configuration': 'Serial 1', 'a_filter_1_type': 'LP 24 dB',
                                                  'a_filter_1_cutoff': 300.0}))
        y = host.render([(0.0, 1.5, 57, 100)], 2.0).mean(axis=1)[SR // 2:int(1.5 * SR)]
        spec = np.abs(np.fft.rfft(y)) ** 2
        f = np.fft.rfftfreq(len(y), 1 / SR)
        self.assertGreater(spec.sum(), 0)
        self.assertLess(10 * np.log10(spec[f > 6000].sum() / spec.sum()), -60)

    def test_fresh_instances_render_bit_identically(self):
        patch = surge.SurgePatch('det', params={**surge.DETERMINISTIC, 'a_osc_1_unison_voices': '3 voices',
                                                'a_lfo_1_trigger_mode': 'Keytrigger'}, mods=[('a_pitch', surge.SRC_LFO1, 0.2)])
        renders = []
        for _ in range(2):
            host = surge.SurgeHost(_cached_surge())
            host.load(patch)
            renders.append(host.render([(0.0, 0.8, 57, 100), (0.3, 1.0, 64, 80)], 1.2))
        self.assertGreater(np.abs(renders[0]).max(), 0)
        np.testing.assert_array_equal(renders[0], renders[1])


if __name__ == '__main__':
    unittest.main()
