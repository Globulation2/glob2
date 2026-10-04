# SPDX-License-Identifier: GPL-3.0-or-later
"""Music source separation with Demucs: estimated stems from a finished stereo mix.

Pipeline role: when a source track has no published stems, ``separate`` estimates
them, and ``adapt.stems.remix`` re-balances the mix per mood. This is the only module
that needs torch, and it imports it inside its functions, so the rest of the package
and the tests run without the separation extras (``requirements-separation.txt``).

API::

    from glob2music.adapt import demucs
    parts = demucs.separate(mix, 'htdemucs', cache_dir=ctx.work_dir / 'demucs')
    parts['drums'], parts['bass'], parts['other'], parts['vocals']   # (frames, 2) float64

* ``mix`` is the decoded ``(frames, 2)`` array at 48 kHz (``adapt.stems.decode``),
  not a path. Separating the very array the recipe mixes with keeps stems and mix on
  one timeline to the frame; no decoder offset can creep in.
* ``model`` is ``'htdemucs'`` (drums, bass, other, vocals: the cleanest drums and
  bass) or ``'htdemucs_6s'`` (adds guitar and piano estimates; its "other" is then
  the sustained and melodic layer, useful to strip an arrangement down, but its
  reconstruction is rougher: -15 dB residual against -28 dB for ``htdemucs`` on
  Curious Critters).
* Stems are cached as float32 ``.npy`` files under ``cache_dir``, keyed by a hash of
  the input samples and every parameter, so a rebuild separates again only when
  something changed.
* Determinism: the ``shifts`` averaging draws random time offsets from Python's
  ``random`` module; ``separate`` seeds it and torch with ``seed``, so the same input
  and parameters give the same stems on the same device. GPU and CPU results differ
  only in the last bits. ``GLOB2MUSIC_DEMUCS_DEVICE`` (e.g. ``cpu``, ``cuda:1``)
  overrides the device choice.

Weights: Demucs 4.1.0 downloads them on first use from the Hugging Face repositories
``adefossez/HTDemucs`` and ``adefossez/HTDemucs-6s`` (published by Demucs's author;
the code is MIT and no separate weights licence is stated). They are a tool: nothing
of them ends up in a shipped file, and separated audio carries only the source
track's licence.
"""
import hashlib
import json
import logging
import os
from pathlib import Path

import numpy as np

from ..spec import SAMPLE_RATE

log = logging.getLogger('glob2music.adapt.demucs')

#: Stem names per supported model, in Demucs's output order.
MODELS = {
    'htdemucs': ('drums', 'bass', 'other', 'vocals'),
    'htdemucs_6s': ('drums', 'bass', 'other', 'vocals', 'guitar', 'piano'),
}

#: Separation settings of the approved sets: the Demucs CLI defaults except
#: ``shifts=2`` (two randomly offset passes averaged, which softens artefacts).
DEFAULTS = {'shifts': 2, 'overlap': 0.25, 'seed': 0}


def run_model(net, mix, shifts=2, overlap=0.25, seed=0, device='cpu'):
    """Separate ``mix`` with a loaded Demucs model; returns ``{stem: (frames, 2)}``.

    Mirrors ``demucs.api.Separator.separate_tensor``: the input is normalised by the
    mean and standard deviation of its mono sum, separated in overlapping segments
    by ``apply_model``, and de-normalised. Kept apart from ``separate`` so tests can
    run Demucs's tiny untrained test model without downloading weights.
    """
    import random
    import torch
    from demucs.apply import apply_model
    from scipy.signal import resample_poly
    from math import gcd
    original_frames = len(mix)
    divisor = gcd(net.samplerate, SAMPLE_RATE)
    if net.samplerate != SAMPLE_RATE:
        mix = resample_poly(mix, net.samplerate // divisor, SAMPLE_RATE // divisor, axis=0)
    random.seed(seed)
    torch.manual_seed(seed)
    wav = torch.from_numpy(np.ascontiguousarray(np.asarray(mix, dtype=np.float32).T))
    ref = wav.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    with torch.no_grad():
        out = apply_model(net, ((wav - mean) / std)[None], shifts=shifts, split=True, overlap=overlap,
                          device=device, progress=False)
    out = (out * std + mean)[0].cpu().numpy()
    stems = {}
    for i, name in enumerate(net.sources):
        y = out[i].T.astype(np.float64)
        if net.samplerate != SAMPLE_RATE:
            y = resample_poly(y, SAMPLE_RATE // divisor, net.samplerate // divisor, axis=0)
        stems[name] = y[:original_frames]
    return stems


def _cache_key(mix, model, params):
    import demucs
    h = hashlib.sha256(np.ascontiguousarray(mix, dtype=np.float32).tobytes())
    h.update(json.dumps([model, params, demucs.__version__, SAMPLE_RATE], sort_keys=True).encode())
    return h.hexdigest()[:20]


def separate(mix, model='htdemucs', cache_dir=None, device=None, **params):
    """Estimated stems of ``mix`` as ``{name: (frames, 2) float64}``, cached.

    ``params`` override ``DEFAULTS`` (``shifts``, ``overlap``, ``seed``). With
    ``cache_dir`` the stems are stored under ``<cache_dir>/<model>-<key>/`` and reused
    while the input and parameters are unchanged.
    """
    if model not in MODELS:
        raise ValueError(f'unsupported Demucs model {model!r}; choose one of {sorted(MODELS)}')
    p = {**DEFAULTS, **params}
    mix = np.asarray(mix, dtype=np.float64)
    folder = Path(cache_dir) / f'{model}-{_cache_key(mix, model, p)}' if cache_dir else None
    if folder and all((folder / f'{n}.npy').exists() for n in MODELS[model]):
        return {n: np.load(folder / f'{n}.npy').astype(np.float64) for n in MODELS[model]}
    import torch
    from demucs.pretrained import get_model
    device = device or os.environ.get('GLOB2MUSIC_DEMUCS_DEVICE') or ('cuda' if torch.cuda.is_available() else 'cpu')
    log.info('separating %.1f s with %s on %s (shifts=%d)', len(mix) / SAMPLE_RATE, model, device, p['shifts'])
    stems = run_model(get_model(model), mix, p['shifts'], p['overlap'], p['seed'], device)
    if folder:
        folder.mkdir(parents=True, exist_ok=True)
        for n, y in stems.items():
            _save_atomic(folder / f'{n}.npy', y.astype(np.float32))
        (folder / 'params.json').write_text(json.dumps({'model': model, **p}, indent=1))
    return stems


def _save_atomic(path, array):
    """``np.save`` to a temporary name, then rename: an interrupted run never leaves a
    truncated stem that a later build would load as a cache hit."""
    tmp = path.with_name(path.name + '.part')
    try:
        with open(tmp, 'wb') as f:
            np.save(f, array)
        os.replace(tmp, path)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise
