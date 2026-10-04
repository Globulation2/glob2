# SPDX-License-Identifier: GPL-3.0-or-later
"""Automatic quality checks for Glob2 soundtrack trios (``glob2music.qa``).

Pipeline role: ``build`` runs the suite on every freshly encoded set, ``install``
refuses a set that fails, and ``python3 -m glob2music check <dir>`` runs it on any
directory holding ``a1.opus``/``a2.opus``/``a3.opus`` (including sets made outside the
pipeline). It needs only numpy, scipy, soundfile, pyloudnorm and librosa, and checks
an 80 s trio in roughly 30 s on one CPU core (the 111 s woodland in about 40-50 s).

The checks, in report order (each module's docstring explains what it measures, why
it matters for the game's position-aligned, looping mood mixer, and how its
threshold was calibrated):

=========== ================================================== ==============
check       measures                                           can fail?
=========== ================================================== ==============
format      Ogg/Opus, 48 kHz, stereo, one stream, equal    yes
            frame counts, 50-120 s
loudness    integrated LUFS vs -18/-17/-16, true peak, ladder  yes
seam        click, spectral jump, level jump at the wrap       yes
repetition  envelope self-similarity, exact internal copies   yes
alignment   onset lag, tempo drift, harmony between moods      yes
contrast    mood distance, combat escalation, calm restraint   yes
noise       HF noise floor (hiss, fizz)                        yes
balance     low-mid mud, brightness                            warn only
audibility  quietest 3 s window                                warn only
dropout     sudden silences in sustained material              warn only
=========== ================================================== ==============

Thresholds live in ``glob2music.spec.QAThresholds``. Sets cannot change them, only
waive a named measure in ``set.toml`` with a reason (``Report.apply_waivers``).

Programmatic use::

    from glob2music.qa import run_checks
    report = run_checks('out/moss-lanterns', waivers={})
    print(report.format_table()); report.failed
"""
from pathlib import Path
import time

from ..audio import Trio
from ..spec import DEFAULT_SPEC
from . import alignment, contrast, fileformat, level, loudness, repetition, seam, spectral
from .analysis import TrioAudio
from .result import CheckResult, FAIL, Measure, Report

#: (name, function(TrioAudio, TrioSpec) -> CheckResult), in report order.
CHECKS = (
    ('format', fileformat.check),
    ('loudness', loudness.check),
    ('seam', seam.check),
    ('repetition', repetition.check),
    ('alignment', alignment.check),
    ('contrast', contrast.check),
    ('noise', spectral.check_noise),
    ('balance', spectral.check_balance),
    ('audibility', level.check_audibility),
    ('dropout', level.check_dropout),
)
CHECK_NAMES = tuple(name for name, _ in CHECKS)

__all__ = ['CHECKS', 'CHECK_NAMES', 'CheckResult', 'Measure', 'Report', 'TrioAudio', 'run_checks']


def run_checks(target, spec=DEFAULT_SPEC, waivers=None, only=None):
    """Run the QA suite and return a ``Report``.

    ``target`` is a directory holding a1/a2/a3.opus, a ``TrioAudio`` or an in-memory
    ``Trio`` (container checks are then skipped). ``waivers`` maps measure names or
    prefixes to reasons. ``only`` restricts the run to some check names. A check that
    raises is reported as a ``fail`` of ``<check>.error`` rather than aborting the
    suite, so one broken file still yields a complete report.
    """
    t0 = time.time()
    if isinstance(target, Trio):
        audio = TrioAudio.from_trio(target)
    elif isinstance(target, TrioAudio):
        audio = target
    else:
        audio = TrioAudio.load(Path(target))
    report = Report(audio.target)
    for name, fn in CHECKS:
        if only and name not in only:
            continue
        if name != 'format' and not audio.present():
            continue
        try:
            report.results.append(fn(audio, spec))
        except Exception as e:  # noqa: BLE001 -- report, never crash the suite
            cr = CheckResult(name)
            cr.add('error', FAIL, type(e).__name__, '', str(e))
            report.results.append(cr)
    if waivers:
        report.apply_waivers(waivers)
    report.seconds = time.time() - t0
    return report
