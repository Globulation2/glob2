# SPDX-License-Identifier: GPL-3.0-or-later
"""Decoded audio plus lazily computed, shared features for the QA checks.

Several checks need the same expensive representations (a 22.05 kHz mono copy, its
STFT, the harmonic/percussive split, onset envelopes, chroma). ``MoodAudio`` computes
each one on first use and caches it, so the full suite decodes and analyses each file
once; a trio of 80 s files takes roughly 30 s on one CPU core.

``TrioAudio`` is deliberately *not* a ``glob2music.audio.Trio``: QA must be able to
inspect broken input (mismatched lengths, wrong sample rate, mono) and report it,
where ``Trio`` refuses to exist.
"""
from functools import cached_property
from pathlib import Path

import librosa
import numpy as np
import scipy.signal as ss
import soundfile as sf

from ..audio import read_audio, trio_paths
from ..spec import FILENAMES, MOODS

#: Analysis rate for the music-level features (onsets, chroma, HPSS). Content above
#: 11 kHz is irrelevant there; the noise check uses the full-rate signal instead.
SR_ANALYSIS = 22050
N_FFT = 2048
HOP = 512


class MoodAudio:
    """One decoded mood file and its cached features."""

    def __init__(self, mood, samples, sample_rate, path=None, info=None):
        self.mood = mood
        self.samples = samples              # float32 (frames, channels) as decoded
        self.sample_rate = sample_rate
        self.path = Path(path) if path else None
        self.info = info                    # soundfile.info() of the file, if any

    # -- basic views ---------------------------------------------------------
    @property
    def frames(self):
        return len(self.samples)

    @property
    def seconds(self):
        return self.frames / self.sample_rate

    @cached_property
    def stereo(self):
        """float64 (frames, 2); mono files are duplicated."""
        y = self.samples.astype(np.float64)
        return np.repeat(y, 2, axis=1) if y.shape[1] == 1 else y[:, :2]

    @cached_property
    def mono(self):
        """float64 mono at the file's own rate."""
        return self.stereo.mean(axis=1)

    @cached_property
    def mono22(self):
        """float32 mono at ``SR_ANALYSIS``."""
        return librosa.resample(self.mono.astype(np.float32), orig_sr=self.sample_rate,
                                target_sr=SR_ANALYSIS, res_type='soxr_hq')

    # -- spectra -------------------------------------------------------------
    @cached_property
    def mag22(self):
        """|STFT| of ``mono22`` (N_FFT, HOP, centred), shape (bins, frames)."""
        return np.abs(librosa.stft(self.mono22, n_fft=N_FFT, hop_length=HOP))

    @cached_property
    def hpss(self):
        """(harmonic, percussive) magnitude spectrograms (librosa median-filter HPSS)."""
        return librosa.decompose.hpss(self.mag22)

    @cached_property
    def percussive_share(self):
        """Fraction of spectral energy in the percussive HPSS component."""
        h, p = self.hpss
        eh, ep = float((h ** 2).sum()), float((p ** 2).sum())
        return ep / (eh + ep + 1e-20)

    @cached_property
    def power_full(self):
        """(freqs, power) of a full-rate, 2048/1024 STFT of the mono mix (noise check)."""
        f, _, z = ss.stft(self.mono, self.sample_rate, nperseg=2048, noverlap=1024, boundary=None)
        return f, np.abs(z) ** 2 + 1e-20

    @cached_property
    def logmel(self):
        """64-band log-mel (dB) from ``mag22``, shape (64, frames)."""
        mel = librosa.feature.melspectrogram(S=self.mag22 ** 2, sr=SR_ANALYSIS, n_mels=64, fmax=11025)
        return librosa.power_to_db(mel, ref=1.0, amin=1e-10)

    @cached_property
    def chroma_harmonic(self):
        """Chroma of the harmonic HPSS part, (12, frames); drums do not smear it."""
        h, _ = self.hpss
        return librosa.feature.chroma_stft(S=h ** 2, sr=SR_ANALYSIS, n_fft=N_FFT, hop_length=HOP)

    # -- rhythm --------------------------------------------------------------
    @cached_property
    def onset_env_fine(self):
        """Onset strength at hop 64 (2.9 ms) for sub-10 ms alignment work."""
        return librosa.onset.onset_strength(y=self.mono22, sr=SR_ANALYSIS, hop_length=64,
                                            n_fft=1024, center=True)

    @cached_property
    def onset_env(self):
        return librosa.onset.onset_strength(S=librosa.power_to_db(self.mag22 ** 2), sr=SR_ANALYSIS)

    @cached_property
    def onset_rate(self):
        """Detected onsets per second."""
        on = librosa.onset.onset_detect(onset_envelope=self.onset_env, sr=SR_ANALYSIS, hop_length=HOP)
        return len(on) / max(self.seconds, 1e-9)

    # -- bands ---------------------------------------------------------------
    def band_share(self, lo, hi):
        """Fraction of total power between ``lo`` and ``hi`` Hz (full rate)."""
        f, p = self.power_full
        tot = p.sum()
        sel = (f >= lo) & (f < hi)
        return float(p[sel].sum() / tot)


class TrioAudio:
    """The three moods of one set as decoded, possibly malformed, audio."""

    def __init__(self, moods, target='', directory=None):
        self.moods = moods          # {mood: MoodAudio}; a missing file is absent
        self.target = target
        self.directory = Path(directory) if directory else None

    def __getitem__(self, mood):
        return self.moods[mood]

    def present(self):
        """Moods that decoded, in file order."""
        return [m for m in MOODS if m in self.moods]

    @property
    def complete(self):
        return len(self.moods) == len(MOODS)

    @classmethod
    def load(cls, directory):
        """Decode ``a1/a2/a3.opus`` from ``directory``; unreadable files are recorded
        in ``errors`` and left out of ``moods``."""
        directory = Path(directory)
        moods, errors = {}, {}
        for mood, path in trio_paths(directory).items():
            if not path.exists():
                errors[mood] = f'{FILENAMES[mood]} missing'
                continue
            try:
                info = sf.info(str(path))
                y, sr = read_audio(path)
                if info.channels == 1:
                    y = y[:, :1]
            except (RuntimeError, sf.LibsndfileError) as e:
                errors[mood] = f'{FILENAMES[mood]} unreadable: {e}'
                continue
            moods[mood] = MoodAudio(mood, y, sr, path, info)
        t = cls(moods, str(directory), directory)
        t.errors = errors
        return t

    @classmethod
    def from_trio(cls, trio, target='<in-memory trio>'):
        """Wrap an in-memory ``Trio`` (no files: format checks only see arrays)."""
        moods = {m: MoodAudio(m, trio[m], trio.sample_rate) for m in MOODS}
        t = cls(moods, target)
        t.errors = {}
        return t
