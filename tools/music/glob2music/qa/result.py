# SPDX-License-Identifier: GPL-3.0-or-later
"""Structured QA results: ``Measure`` (one judged number), ``CheckResult`` (one check)
and ``Report`` (a whole trio), with waiver handling and table/JSON output.

Every check judges one or more *measures*, each named ``<check>.<item>`` (for
example ``loudness.calm.target`` or ``seam.combat.step``). A measure's status is

* ``pass`` -- inside the good band;
* ``warn`` -- worth a listen, never blocks a build;
* ``fail`` -- blocks ``check``/``build``/``install`` (non-zero exit);
* ``waived`` -- would have warned or failed, but the set's ``set.toml`` waives it
  with a written reason; it is reported, never hidden;
* ``info`` -- reported for context only, never judged;
* ``skip`` -- could not be measured (missing input, too short, ...).

A check's own status is the worst status among its measures, and its headline
value/threshold/detail are those of that worst measure, so the summary table shows
the reason a check did not pass.
"""
from dataclasses import asdict, dataclass, field
import json
import math

PASS, WARN, FAIL, WAIVED, INFO, SKIP = 'pass', 'warn', 'fail', 'waived', 'info', 'skip'
# Severity order for "worst of": a waived fail outranks a pass so it stays visible,
# but never outranks a live warn or fail.
_SEVERITY = {INFO: 0, SKIP: 1, PASS: 2, WAIVED: 3, WARN: 4, FAIL: 5}


def worst(statuses, default=PASS):
    statuses = list(statuses)
    return max(statuses, key=_SEVERITY.__getitem__) if statuses else default


def band(value, warn, fail, higher_is_worse=True):
    """Status of ``value`` against warn/fail limits (inclusive on the good side)."""
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return SKIP
    if higher_is_worse:
        return FAIL if value > fail else WARN if value > warn else PASS
    return FAIL if value < fail else WARN if value < warn else PASS


@dataclass
class Measure:
    """One judged quantity. ``threshold`` is a short human-readable rule."""

    name: str
    status: str
    value: object = None
    threshold: str = ''
    detail: str = ''
    unit: str = ''
    waiver: str = ''

    def formatted_value(self):
        v = self.value
        if isinstance(v, float):
            if math.isnan(v):
                return 'n/a'
            if math.isinf(v):
                return '-inf' if v < 0 else 'inf'
            text = f'{v:.3f}' if abs(v) < 10 else f'{v:.1f}'
        elif v is None:
            text = '-'
        else:
            text = str(v)
        return f'{text} {self.unit}'.strip()


@dataclass
class CheckResult:
    """The outcome of one named check: its measures plus a headline."""

    name: str
    measures: list = field(default_factory=list)
    description: str = ''

    def add(self, item, status, value=None, threshold='', detail='', unit=''):
        """Append a measure named ``<check>.<item>`` and return it."""
        m = Measure(f'{self.name}.{item}', status, _plain(value), threshold, detail, unit)
        self.measures.append(m)
        return m

    @property
    def status(self):
        return worst((m.status for m in self.measures), default=SKIP)

    @property
    def headline(self):
        """The worst measure (first among equals), or None."""
        if not self.measures:
            return None
        s = self.status
        return next(m for m in self.measures if m.status == s)

    @property
    def value(self):
        h = self.headline
        return h.value if h else None

    @property
    def threshold(self):
        h = self.headline
        return h.threshold if h else ''

    @property
    def detail(self):
        h = self.headline
        return f'{h.name}: {h.detail}'.rstrip(': ') if h else ''

    def to_dict(self):
        return {'name': self.name, 'status': self.status, 'value': _plain(self.value),
                'threshold': self.threshold, 'detail': self.detail,
                'measures': [asdict(m) for m in self.measures]}


@dataclass
class Report:
    """All check results for one trio, plus the waivers that were applied."""

    target: str
    results: list = field(default_factory=list)
    waivers: dict = field(default_factory=dict)
    seconds: float = 0.0

    @property
    def status(self):
        return worst((r.status for r in self.results), default=SKIP)

    @property
    def failed(self):
        return any(r.status == FAIL for r in self.results)

    def measure(self, name):
        """Look up a measure by full name (``'seam.calm.step'``), or None."""
        for r in self.results:
            for m in r.measures:
                if m.name == name:
                    return m
        return None

    def apply_waivers(self, waivers):
        """Turn warn/fail into ``waived`` for every measure a waiver key covers.

        A key covers a measure when it equals the measure name or is a dotted
        prefix of it: ``loudness`` waives the whole check, ``loudness.ladder`` one
        rule, ``seam.combat`` one mood of one check. Every waiver needs a non-empty
        reason (``manifest.py`` enforces this when loading ``set.toml``). Keys that
        match nothing are reported as an ``info`` measure under ``waivers`` so stale
        waivers get noticed; a key whose check did not run (``check --only``) is not
        stale, merely untested.
        """
        self.waivers = dict(waivers)
        used = set()
        for r in self.results:
            for m in r.measures:
                for key, reason in waivers.items():
                    if m.name == key or m.name.startswith(key + '.'):
                        used.add(key)
                        if m.status in (WARN, FAIL):
                            m.waiver = f'{m.status} waived: {reason}'
                            m.status = WAIVED
        ran = {r.name for r in self.results}
        stale = sorted(k for k in set(waivers) - used if k.split('.', 1)[0] in ran)
        if stale:
            cr = CheckResult('waivers')
            for key in stale:
                cr.add(key.replace('.', '_'), INFO, key, '', 'waiver matches no measure')
            self.results.append(cr)

    def to_dict(self):
        return {'target': self.target, 'status': self.status, 'seconds': round(self.seconds, 2),
                'waivers': self.waivers, 'checks': [r.to_dict() for r in self.results]}

    def to_json(self, indent=1):
        """Strict JSON: NaN and infinities become null."""
        return json.dumps(_finite(self.to_dict()), indent=indent, allow_nan=False)

    def format_table(self, verbose=False):
        """A fixed-width table: one row per check, or per measure with ``verbose``.

        Without ``verbose``, measures that warn, fail or are waived are still
        listed under their check so the reason is always on screen.
        """
        rows = [('check', 'status', 'value', 'threshold', 'detail')]
        for r in self.results:
            h = r.headline
            rows.append((r.name, r.status.upper(), h.formatted_value() if h else '-',
                         r.threshold, r.detail))
            for m in r.measures:
                if verbose or (m.status in (WARN, FAIL, WAIVED) and m is not h):
                    rows.append(('  ' + m.name, m.status, m.formatted_value(), m.threshold,
                                 m.waiver or m.detail))
        widths = [min(max(len(row[i]) for row in rows), cap) for i, cap in enumerate((30, 6, 16, 26, 200))]
        lines = [f'QA report: {self.target}']
        for k, row in enumerate(rows):
            cells = [cell if len(cell) <= w else cell[:w - 1] + '~' for cell, w in zip(row, widths)]
            lines.append('  '.join(c.ljust(w) for c, w in zip(cells, widths)).rstrip())
            if k == 0:
                lines.append('  '.join('-' * w for w in widths))
        lines.append(f'overall: {self.status.upper()} ({self.seconds:.1f} s)')
        return '\n'.join(lines)


def _plain(v):
    """Convert numpy scalars to plain Python for JSON."""
    if hasattr(v, 'item') and not isinstance(v, (list, dict, str)):
        try:
            return v.item()
        except (ValueError, AttributeError):
            return v
    return v


def _finite(obj):
    if isinstance(obj, float) and not math.isfinite(obj):
        return None
    if isinstance(obj, dict):
        return {k: _finite(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_finite(v) for v in obj]
    return obj
