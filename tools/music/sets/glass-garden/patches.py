# SPDX-License-Identifier: GPL-3.0-or-later
"""Glass Garden's Surge XT patches, designed for this set (no factory presets).

Each patch starts from Surge's CC0 Init state; see ``glob2music.backends.surge`` for the
three layers (host ``params`` in display units, ``xml`` internal values, ``mods``
routings) and for the filter enable fix these patches depend on. Macro 1 is the
"brightness" lane and macro 2 the "drive" lane that ``composition.MACROS`` automates.

Every patch includes ``DETERMINISTIC`` (no analog drift, oscillators retriggered on
each note) and uses key-triggered LFOs, so renders are
bit-identical; see "Determinism" in ``backends/surge.py``.
"""
from glob2music.backends.surge import (SRC_FILTER_EG as FEG, SRC_LFO1 as LFO1, SRC_LFO2 as LFO2,
                                       SRC_MACRO1 as M1, SRC_MACRO2 as M2, SRC_VELOCITY as VEL, SurgePatch,
                                       DETERMINISTIC)


# FM bell lead: an FM2 operator (modulator ratio 2, 9 %) whose index rises with the
# filter envelope and velocity, so each note starts glassy and settles towards a sine;
# a quiet sine an octave up; 12 dB low-pass at 2.3 kHz; mono with 40 ms fingered
# portamento (legato notes glide); delayed 5.4 Hz vibrato; macro 1 opens the filter.
GLASS_LEAD = SurgePatch('glass_lead', params={
    'a_filter_configuration': 'Serial 1',
    'a_osc_1_type': 'FM2',
    'a_osc_1_m1_amount': 9.0,
    'a_osc_1_m1_ratio': 'C : 2',
    'a_osc_2_type': 'Sine',
    'a_osc_2_octave': 1.0,
    'a_osc_2_mute': False,
    'a_osc_2_volume': -20.0,
    'a_play_mode': 'Mono (Fingered Portamento)',
    'a_portamento': 40.0,
    'a_filter_1_type': 'LP 12 dB',
    'a_filter_1_cutoff': 2300.0,
    'a_filter_1_resonance': 5.0,
    'a_filter_1_keytrack': 50.0,
    'a_filter_eg_attack': 0.0,
    'a_filter_eg_decay': 260.0,
    'a_filter_eg_sustain': 20.0,
    'a_filter_eg_release': 300.0,
    'a_amp_eg_attack': 8.0,
    'a_amp_eg_decay': 1100.0,
    'a_amp_eg_sustain': 60.0,
    'a_amp_eg_release': 420.0,
    'a_velocity_vca_gain': -18.0,
    'a_volume': -6.0,
    'a_lfo_1_type': 'Sine',
    'a_lfo_1_rate': 5.4,
    'a_lfo_1_delay': 300.0,
    'a_lfo_1_attack': 400.0,
    **DETERMINISTIC,
}, xml={'a_filter1_envmod': 8.0},
   mods=[('a_pitch', LFO1, 0.18), ('a_filter1_cutoff', VEL, 8.0), ('a_filter1_cutoff', M1, 8.0), ('a_osc1_param0', FEG, 0.2), ('a_osc1_param0', VEL, 0.08)])

# PWM pad: a 35 % pulse in 2-voice unison whose width drifts per voice (key-triggered
# 0.21 Hz LFO, so voices entering at different times drift apart), a soft sine an octave up, 24 dB low-pass at 950 Hz breathing with a
# slow LFO; macro 1 opens it over phrases. 0.8 s attack, 2.6 s release.
GLASS_PAD = SurgePatch('glass_pad', params={
    'a_filter_configuration': 'Serial 1',
    'a_osc_1_type': 'Classic',
    'a_osc_1_shape': -100.0,
    'a_osc_1_width_1': 35.0,
    'a_osc_1_unison_voices': '2 voices',
    'a_osc_1_unison_detune': 9.0,
    'a_osc_2_type': 'Sine',
    'a_osc_2_octave': 1.0,
    'a_osc_2_mute': False,
    'a_osc_2_volume': -16.0,
    'a_filter_1_type': 'LP 24 dB',
    'a_filter_1_cutoff': 950.0,
    'a_filter_1_resonance': 8.0,
    'a_filter_1_keytrack': 30.0,
    'a_amp_eg_attack': 800.0,
    'a_amp_eg_decay': 2500.0,
    'a_amp_eg_sustain': 85.0,
    'a_amp_eg_release': 2600.0,
    'a_velocity_vca_gain': -12.0,
    'a_volume': -6.0,
    'a_lfo_1_type': 'Triangle',
    'a_lfo_1_rate': 0.21,
    'a_lfo_1_trigger_mode': 'Keytrigger',
    'a_lfo_2_type': 'Sine',
    'a_lfo_2_rate': 0.09,
    'a_lfo_2_trigger_mode': 'Keytrigger',
    **DETERMINISTIC,
}, xml={},
   mods=[('a_osc1_param1', LFO1, 0.25), ('a_filter1_cutoff', LFO2, 3.0), ('a_filter1_cutoff', M1, 14.0), ('a_filter1_cutoff', VEL, 4.0)])

# Woody reed for the counter-line: a 42 % pulse with a sine an octave down through a
# 24 dB low-pass at 1 kHz, mono glide, later and slower vibrato than the lead.
REED_COUNTER = SurgePatch('reed_counter', params={
    'a_filter_configuration': 'Serial 1',
    'a_osc_1_type': 'Classic',
    'a_osc_1_shape': -100.0,
    'a_osc_1_width_1': 42.0,
    'a_osc_2_type': 'Sine',
    'a_osc_2_octave': -1.0,
    'a_osc_2_mute': False,
    'a_osc_2_volume': -12.0,
    'a_play_mode': 'Mono (Fingered Portamento)',
    'a_portamento': 30.0,
    'a_filter_1_type': 'LP 24 dB',
    'a_filter_1_cutoff': 1000.0,
    'a_filter_1_resonance': 20.0,
    'a_filter_1_keytrack': 55.0,
    'a_filter_eg_attack': 60.0,
    'a_filter_eg_decay': 600.0,
    'a_filter_eg_sustain': 50.0,
    'a_amp_eg_attack': 70.0,
    'a_amp_eg_decay': 600.0,
    'a_amp_eg_sustain': 80.0,
    'a_amp_eg_release': 320.0,
    'a_velocity_vca_gain': -16.0,
    'a_volume': -6.0,
    'a_lfo_1_type': 'Sine',
    'a_lfo_1_rate': 4.4,
    'a_lfo_1_delay': 380.0,
    'a_lfo_1_attack': 600.0,
    **DETERMINISTIC,
}, xml={'a_filter1_envmod': 6.0},
   mods=[('a_pitch', LFO1, 0.15), ('a_filter1_cutoff', VEL, 8.0), ('a_filter1_cutoff', M1, 8.0)])

# Arpeggio pluck: detuned saw plus square sub through Surge's vintage ladder at
# 280 Hz, snapped open by the filter envelope (+20 semitones); macro 1 opens the base
# cutoff, macro 2 lengthens the envelope decay (more legato and urgent in combat).
PULSE_PLUCK = SurgePatch('pulse_pluck', params={
    'a_filter_configuration': 'Serial 1',
    'a_osc_1_type': 'Classic',
    'a_osc_1_shape': 0.0,
    'a_osc_1_unison_voices': '2 voices',
    'a_osc_1_unison_detune': 9.0,
    'a_osc_2_type': 'Classic',
    'a_osc_2_shape': -100.0,
    'a_osc_2_octave': -1.0,
    'a_osc_2_mute': False,
    'a_osc_2_volume': -8.0,
    'a_filter_1_type': 'LP Vintage Ladder',
    'a_filter_1_cutoff': 280.0,
    'a_filter_1_resonance': 18.0,
    'a_filter_1_keytrack': 30.0,
    'a_filter_eg_attack': 0.0,
    'a_filter_eg_decay': 210.0,
    'a_filter_eg_sustain': 0.0,
    'a_filter_eg_release': 150.0,
    'a_amp_eg_attack': 0.0,
    'a_amp_eg_decay': 520.0,
    'a_amp_eg_sustain': 0.0,
    'a_amp_eg_release': 220.0,
    'a_velocity_vca_gain': -20.0,
    'a_volume': -6.0,
    **DETERMINISTIC,
}, xml={'a_filter1_envmod': 20.0},
   mods=[('a_filter1_cutoff', VEL, 9.0), ('a_filter1_cutoff', M1, 12.0), ('a_env2_decay', M2, 1.0)])

# Round bass: sine body plus a filtered saw through a 24 dB low-pass at 260 Hz and a
# soft waveshaper whose drive rises a little with macro 2 (combat).
ROUND_BASS = SurgePatch('round_bass', params={
    'a_filter_configuration': 'Serial 1',
    'a_osc_1_type': 'Sine',
    'a_osc_2_type': 'Classic',
    'a_osc_2_shape': 0.0,
    'a_osc_2_mute': False,
    'a_osc_2_volume': -7.0,
    'a_play_mode': 'Mono',
    'a_portamento': 12.0,
    'a_filter_1_type': 'LP 24 dB',
    'a_filter_1_cutoff': 260.0,
    'a_filter_1_resonance': 22.0,
    'a_filter_1_keytrack': 30.0,
    'a_filter_eg_attack': 0.0,
    'a_filter_eg_decay': 180.0,
    'a_filter_eg_sustain': 25.0,
    'a_filter_eg_release': 120.0,
    'a_amp_eg_attack': 5.0,
    'a_amp_eg_decay': 700.0,
    'a_amp_eg_sustain': 72.0,
    'a_amp_eg_release': 140.0,
    'a_waveshaper_type': 'Soft',
    'a_waveshaper_drive': -6.0,
    'a_velocity_vca_gain': -14.0,
    'a_volume': -4.0,
    **DETERMINISTIC,
}, xml={'a_filter1_envmod': 20.0},
   mods=[('a_filter1_cutoff', VEL, 8.0), ('a_filter1_cutoff', M1, 10.0), ('a_ws_drive', M2, 5.0)])
