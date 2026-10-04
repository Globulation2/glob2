# SPDX-License-Identifier: GPL-3.0-or-later
"""Surge XT as a headless rendering backend for symbolic sets, with patches written in code.

Pipeline role: a synth set's ``recipe.py`` builds a ``SurgeBackend`` from its patch table
(``{instrument key: SurgePatch}``) and hands it, with the composition, to
``score.pipeline.build_trio``. The score layer humanises every part into a
``PerformedPart``; ``SurgeBackend.render`` plays each one through Surge XT 1.3.4
(GPL-3.0) hosted as a VST3 in pedalboard and returns stems with ``PREROLL_S`` of
lead-in and ``TAIL_S`` of release tail, which ``loop.fold_tail`` folds into the loop.

Getting Surge. ``surge_vst3`` fetches the official Linux release tarball through
``sources.fetch`` (pinned by URL and SHA-256, cached in ``tools/music/cache``) and
extracts only the VST3 bundle. pedalboard must be 0.9.16 (``requirements-synth.txt``):
0.9.25 crashes with SIGILL on CPUs without AVX-512, such as AMD Zen 1.

Patches. A ``SurgePatch`` starts from Surge's own "Init" state (CC0) and has three layers:

* ``params``: host parameters by pedalboard name, in display units (Hz, ms, dB,
  semitones, %, or an option string such as ``'LP 24 dB'``). Names depend on the
  oscillator type (``a_osc_1_m1_amount`` exists only for FM2), so ``*_type`` and
  ``*_mode`` parameters are applied first.
* ``xml``: values written straight into the patch XML, in Surge's internal units, for
  parameters the host cannot set reliably (the filter envelope amount ``a_filter1_envmod``).
* ``mods``: modulation routings ``(xml target, source id, depth)``. The host interface
  cannot express these, so they are injected into the state XML. Depth is in the
  target's internal units (semitones for pitch and cutoff). Source ids are the
  ``SRC_*`` constants below.

**The filter enable fix.** Surge's Init state ships with filter 1, filter 2 and the
waveshaper *deactivated* (``deactivated="1"`` in the XML), and choosing a filter type
through the host does not reactivate them. Historical note: early renders of this
pipeline's synth sets missed this, played raw unfiltered saws and pulses, and
listeners heard them as "scratchy". This
module re-enables every stage a patch selects (``_activate_stages``) and verifies after
loading that the patch's parameters stuck.

Determinism. Four things make Surge renders differ from run to run: analog drift
(``a_osc_drift``), oscillators that keep a free-running phase between notes, LFOs in
``'Random'`` mode, and ``'Freerun'`` LFOs (their phase follows a clock shared by every
instance in the process); an instance's own state also carries over between renders.
Patches must therefore set drift to 0, retrigger every oscillator (``DETERMINISTIC``)
and use only ``'Keytrigger'`` LFOs, all checked by ``check_deterministic``, and
``SurgeBackend`` renders every part in a freshly loaded instance. Without these rules,
repeated renders of the same events differed by up to about -22 dB relative to the
signal. With them, short renders are bit-identical across instances, processes and
part order (``tests/test_surge.py``); in full Glass Garden builds most stems are
bit-identical and the rest (3 of 14, all with macro automation) differ by -104 to
-112 dBFS, about 80 dB under the signal, from a cause not yet identified. Vorbis
encoding magnifies that to a run-to-run difference of about -70 dBFS in the decoded
files, which is inaudible but means the shipped Oggs, not a rebuild, are the exact
reference.

Automation. ``SurgeBackend`` takes ``automation(mood, part_name, seconds) -> {'m1': x,
'm2': y}``, applied every ``BLOCK`` samples to Surge's macros (0..1). Patches route the
macros (``SRC_MACRO1``/``2``) to cutoff, drive or decay, so timbre moves over phrases.
Surge does not map CC11 by default and this backend does not apply a part's CC11
curve; a set that wants a fader ride applies it in its mix (Glass Garden does). Blocks
are aligned to the loop start (the first block covers the rest of the pre-roll), so
automation is sampled at the same instants as in a render that starts at the loop.

Patches can be exported as standard ``.fxp`` files (``write_fxp``) and opened in Surge
XT's patch browser for editing by ear.
"""
import logging
import re
import struct
import tarfile
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from ..sources import DEFAULT_CACHE, fetch
from ..spec import SAMPLE_RATE

log = logging.getLogger(__name__)

SURGE_VERSION = '1.3.4'
SURGE_URL = ('https://github.com/surge-synthesizer/releases-xt/releases/download/'
             f'{SURGE_VERSION}/surge-xt-linux-x86_64-{SURGE_VERSION}.tar.gz')
SURGE_SHA256 = 'fdd578eea384f5ec1b40cd26936b213dc75438a18a787b21227f941d2a680ecd'
#: Path of the plug-in bundle inside the release tarball.
VST3_MEMBER = 'lib/vst3/Surge XT.vst3'

#: Surge modulation source ids (``modsources`` in Surge's source).
SRC_VELOCITY, SRC_MACRO1, SRC_MACRO2, SRC_FILTER_EG = 1, 7, 8, 16
SRC_LFO1, SRC_LFO2 = 17, 18

PREROLL_S = 0.5          # lead-in before the loop (humanised notes may start early)
TAIL_S = 6.0             # release and pad tails after the loop end
BLOCK = 1024             # automation resolution, samples (~23 ms)

#: Host parameters set before the rest: changing them renames or resets others.
_FIRST = ('_type', '_mode', '_configuration')
#: Host parameter -> XML element of each stage that Init ships deactivated.
_STAGES = {'a_filter_1_type': 'a_filter1_type', 'a_filter_2_type': 'a_filter2_type',
           'a_waveshaper_type': 'a_ws_type'}


#: Host parameters every patch needs for bit-identical renders.
DETERMINISTIC = {'a_osc_drift': 0.0, 'a_osc_1_retrigger': True, 'a_osc_2_retrigger': True, 'a_osc_3_retrigger': True}


def check_deterministic(patch):
    """Raise ``ValueError`` unless ``patch`` follows the determinism rules above."""
    bad = [k for k, v in DETERMINISTIC.items() if patch.params.get(k) != v]
    bad += [k for k, v in patch.params.items() if k.endswith('_trigger_mode') and v != 'Keytrigger']
    if bad:
        raise ValueError(f'{patch.name}: not deterministic, fix {bad} (see backends/surge.py, "Determinism")')


@dataclass
class SurgePatch:
    """One designed patch (see the module docstring for the three layers)."""

    name: str
    params: dict
    mods: list = field(default_factory=list)
    xml: dict = field(default_factory=dict)


def surge_vst3(cache_dir=DEFAULT_CACHE, offline=False):
    """Path of the Surge XT VST3 bundle, fetching and extracting the release once."""
    root = Path(cache_dir) / f'surge-xt-{SURGE_VERSION}'
    bundle = root / VST3_MEMBER
    if not bundle.exists():
        tarball = fetch(SURGE_URL, SURGE_SHA256, cache_dir=cache_dir, offline=offline)
        log.info('extracting %s from %s', VST3_MEMBER, tarball.name)
        with tarfile.open(tarball) as tar:
            members = [m for m in tar.getmembers() if m.name.lstrip('./').startswith(VST3_MEMBER)]
            tar.extractall(root, members=members, filter='data')
    return bundle


# ----------------------------------------------------------------------------- state blobs
# pedalboard exposes a VST3 plug-in's state as JUCE writes it: b'VC2!', a little-endian
# length, then XML whose <IComponent> element holds the plug-in's own state in JUCE's
# MemoryBlock base64 dialect. Surge's own state is a 'sub3' patch header (tag, XML size,
# six wavetable sizes) followed by the patch XML and private trailing data.

_ALPHABET = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'
_INDEX = {c: i for i, c in enumerate(_ALPHABET)}


def juce_b64decode(text):
    """Decode JUCE ``MemoryBlock::toBase64Encoding`` text (``'<size>.<chars>'``)."""
    size_s, data = text.split('.', 1)
    size = int(size_s)
    bits = 0
    nbits = 0
    out = bytearray()
    for ch in data:                      # 6 bits per char, least significant bit first
        bits |= _INDEX[ch] << nbits
        nbits += 6
        while nbits >= 8 and len(out) < size:
            out.append(bits & 0xFF)
            bits >>= 8
            nbits -= 8
    return bytes(out.ljust(size, b'\0'))


def juce_b64encode(data):
    """Inverse of ``juce_b64decode``."""
    out = []
    bits = int.from_bytes(data, 'little')
    for _ in range((len(data) * 8 + 5) // 6):
        out.append(_ALPHABET[bits & 0x3F])
        bits >>= 6
    return f'{len(data)}.' + ''.join(out)


def unwrap_state(raw):
    """VST3 state blob -> Surge's component state (``sub3`` header + XML + extras)."""
    xml = raw[8:].decode('utf-8').rstrip('\0')
    return juce_b64decode(re.search(r'<IComponent>([^<]*)</IComponent>', xml).group(1))


def wrap_state(template_raw, component):
    """Put ``component`` into the VST3 state blob ``template_raw``."""
    xml = template_raw[8:].decode('utf-8').rstrip('\0')
    xml = re.sub(r'<IComponent>[^<]*</IComponent>', lambda m: f'<IComponent>{juce_b64encode(component)}</IComponent>', xml)
    body = xml.encode('utf-8') + b'\0'
    return b'VC2!' + struct.pack('<I', len(body)) + body


def split_component(component):
    """``(32-byte header, patch XML text, trailing bytes)``."""
    size = struct.unpack('<I', component[4:8])[0]
    return component[:32], component[32:32 + size].decode('utf-8'), component[32 + size:]


def join_component(header, xml, tail):
    """Inverse of ``split_component``; fixes the XML size in the header."""
    body = xml.encode('utf-8')
    return header[:4] + struct.pack('<I', len(body)) + header[8:] + body + tail


def _activate_stages(xml, params):
    """The filter enable fix: clear ``deactivated="1"`` on every filter/waveshaper stage
    whose type the patch sets (to anything but ``'Off'``)."""
    for host, element in _STAGES.items():
        if params.get(host, 'Off') != 'Off':
            xml = re.sub(r'(<%s [^>]*?)deactivated="1"' % element, r'\1deactivated="0"', xml, count=1)
    return xml


def edit_patch_xml(xml, patch):
    """Apply a ``SurgePatch``'s name, stage activation, ``xml`` values and ``mods`` to
    patch XML read back from Surge after its host parameters were set."""
    xml = re.sub(r'<meta name="[^"]*"', f'<meta name="{patch.name}"', xml, count=1)
    xml = _activate_stages(xml, patch.params)
    for element, value in patch.xml.items():
        xml, n = re.subn(r'(<%s [^>]*?value=")[^"]*(")' % re.escape(element),
                         lambda m: f'{m.group(1)}{value:.6f}{m.group(2)}', xml, count=1)
        if n != 1:
            raise KeyError(f'{patch.name}: no XML parameter {element}')
    targets = {}
    for element, source, depth in patch.mods:
        targets.setdefault(element, []).append((source, depth))
    for element, routes in targets.items():
        m = re.search(r'<%s ([^>]*?)\s*/>' % re.escape(element), xml)
        if not m:
            raise KeyError(f'{patch.name}: no XML parameter {element} to modulate')
        inner = ''.join(f'<modrouting source="{s}" depth="{d:.6f}" muted="0" source_index="0" source_scene="0" />'
                        for s, d in routes)
        xml = xml[:m.start()] + f'<{element} {m.group(1)}>{inner}</{element}>' + xml[m.end():]
    return xml


def write_fxp(component, path, name):
    """Save a component state as a VST2 opaque-chunk program (``.fxp``) for Surge XT."""
    header, xml, _ = split_component(component)
    chunk = join_component(header, xml, b'')
    body = (b'FPCh' + struct.pack('>iiii', 1, int.from_bytes(b'cjs3', 'big'), 1, 1)
            + name.encode()[:27].ljust(28, b'\0') + struct.pack('>i', len(chunk)) + chunk)
    Path(path).write_bytes(b'CcnK' + struct.pack('>i', len(body)) + body)


# ----------------------------------------------------------------------------- host

def _number(text):
    """Display text -> number in base units (ms for times, Hz for frequencies)."""
    m = re.match(r'^(-?[0-9.]+)\s*(ms|s|kHz|Hz|semitones|cents|%|dB)?', str(text).strip())
    if not m:
        return None
    x = float(m.group(1))
    return x * 1000.0 if m.group(2) in ('s', 'kHz') else x


def _matches(current, wanted):
    if isinstance(wanted, bool) or isinstance(current, bool):
        return str(current) == str(wanted)
    if isinstance(wanted, (int, float)):
        x = _number(current)
        return x is not None and abs(x - wanted) <= max(0.06 * abs(wanted), 0.6)
    return str(current) == str(wanted)


class SurgeHost:
    """One loaded Surge XT instance that loads patches and renders MIDI events."""

    def __init__(self, bundle):
        import pedalboard
        self.plugin = pedalboard.load_plugin(str(bundle))
        self.init_state = self.plugin.raw_state

    def _idle(self, seconds=0.1):
        # Surge applies state and type changes on the audio thread: process silence.
        self.plugin([], duration=seconds, sample_rate=SAMPLE_RATE, num_channels=2, buffer_size=BLOCK, reset=False)

    def _set(self, key, value):
        """Set a host parameter by display value, falling back to the nearest valid
        entry for parameters whose values are strings (``'902.5 ms'``)."""
        p = self.plugin
        try:
            setattr(p, key, value)
            if _matches(getattr(p, key), value):
                return
        except (ValueError, TypeError):
            pass
        if isinstance(value, (int, float)):
            options = [(abs(_number(v) - value), v) for v in p.parameters[key].valid_values if _number(v) is not None]
            if options:
                setattr(p, key, min(options)[1])
                return
        raise ValueError(f'cannot set Surge parameter {key} = {value!r}')

    def _apply_params(self, params):
        for key, value in params.items():
            if not _matches(getattr(self.plugin, key), value):
                self._set(key, value)

    def load(self, patch):
        """Load ``patch`` (a ``SurgePatch``); returns Surge's component state for it."""
        p = self.plugin
        p.raw_state = self.init_state
        self._idle(0.25)
        first = {k: v for k, v in patch.params.items() if k.endswith(_FIRST)}
        self._apply_params(first)
        self._idle(0.25)
        self._apply_params(patch.params)
        self._idle()
        header, xml, tail = split_component(unwrap_state(p.raw_state))
        component = join_component(header, edit_patch_xml(xml, patch), tail)
        p.raw_state = wrap_state(self.init_state, component)
        self._idle(1.0)
        # A state reload can drop host edits made just before the state was read back.
        for _ in range(3):
            if all(_matches(getattr(p, k), v) for k, v in patch.params.items()):
                break
            self._apply_params(patch.params)
            self._idle()
        bad = [k for k, v in patch.params.items() if not _matches(getattr(p, k), v)]
        if bad:
            raise RuntimeError(f'{patch.name}: Surge did not accept {[(k, str(getattr(p, k))) for k in bad]}')
        return unwrap_state(p.raw_state)

    def render(self, events, seconds, automation=None, align_s=0.0):
        """Render ``events`` (``[(on_s, off_s, pitch, velocity)]``, times >= 0) for
        ``seconds``; ``automation(t) -> {param: raw 0..1}`` runs at every block start.
        Block boundaries fall on ``align_s + k * BLOCK`` samples."""
        import mido
        msgs = []
        for on, off, pitch, vel in events:
            msgs.append((on, 1, mido.Message('note_on', note=int(pitch), velocity=int(vel))))
            msgs.append((off, 0, mido.Message('note_off', note=int(pitch), velocity=0)))
        msgs.sort(key=lambda m: (m[0], m[1]))
        params = {}
        n = int(round(seconds * SAMPLE_RATE))
        out = np.zeros((n, 2), dtype=np.float64)
        i = pos = 0
        first = int(round(align_s * SAMPLE_RATE)) % BLOCK
        while pos < n:
            size = min(first or BLOCK, n - pos)
            first = 0
            t0, t1 = pos / SAMPLE_RATE, (pos + size) / SAMPLE_RATE
            block = []
            while i < len(msgs) and msgs[i][0] < t1:
                block.append(msgs[i][2].copy(time=max(0.0, msgs[i][0] - t0)))
                i += 1
            if automation:
                for key, value in automation(t0).items():
                    if key not in params:
                        params[key] = self.plugin.parameters[key]
                    params[key].raw_value = float(value)
            y = self.plugin(block, duration=size / SAMPLE_RATE, sample_rate=SAMPLE_RATE, num_channels=2,
                            buffer_size=size, reset=False)
            out[pos:pos + size] = y[:, :size].T
            pos += size
        return out


class SurgeBackend:
    """Renders ``PerformedPart`` objects with Surge XT for ``score.pipeline.build_trio``.

    ``patches`` maps an instrument key to ``(model.Instrument, SurgePatch)``;
    ``automation(mood, part_name, seconds)`` returns macro values (see the module
    docstring) or ``None``. Every part renders in a freshly loaded instance (about
    10 s each), so its audio does not depend on what was rendered before. Stems are
    cached per part in ``work_dir`` keyed by the performance, the patch and the Surge
    version, so an unchanged rebuild skips Surge.
    """

    preroll_s = PREROLL_S
    tail_s = TAIL_S

    def __init__(self, patches, automation=None, cache_dir=DEFAULT_CACHE, offline=False):
        for _, patch in patches.values():
            check_deterministic(patch)
        self.patches = patches
        self.instruments = {k: inst for k, (inst, _) in patches.items()}
        self.automation = automation
        self.bundle = surge_vst3(cache_dir, offline=offline)

    def render(self, performed, loop_seconds, work_dir):
        """``{mood: [PerformedPart]}`` -> ``{mood: {part name: (frames, 2) stem}}``,
        each stem starting ``preroll_s`` before the loop and running ``tail_s`` past it."""
        import hashlib
        import json
        work_dir = Path(work_dir)
        seconds = PREROLL_S + loop_seconds + TAIL_S
        stems = {}
        for mood, parts in performed.items():
            stems[mood] = {}
            for part in parts:
                _, patch = self.patches[part.instrument]
                auto = None
                if self.automation:
                    auto_fn = self.automation
                    auto = (lambda m, name: lambda t: auto_fn(m, name, t - PREROLL_S))(mood, part.name)
                key = hashlib.sha256(json.dumps([part.events, repr(patch), SURGE_VERSION, BLOCK,
                                                 [auto(i * 0.5) for i in range(int(seconds * 2))] if auto else None],
                                                default=str).encode()).hexdigest()
                path = work_dir / mood / f'{part.name}.npy'
                key_file = path.with_suffix('.key')
                if path.exists() and key_file.exists() and key_file.read_text() == key:
                    stems[mood][part.name] = np.load(path)
                    continue
                host = SurgeHost(self.bundle)
                host.load(patch)
                events = [(on + PREROLL_S, off + PREROLL_S, p, v) for on, off, p, v in part.events]
                y = host.render(events, seconds, auto, align_s=PREROLL_S)
                path.parent.mkdir(parents=True, exist_ok=True)
                np.save(path, y)
                key_file.write_text(key)
                stems[mood][part.name] = y
                log.info('surge: rendered %s/%s with %s', mood, part.name, patch.name)
        return stems

    def export_patches(self, directory):
        """Write every patch as ``<name>.fxp`` into ``directory``."""
        Path(directory).mkdir(parents=True, exist_ok=True)
        host = SurgeHost(self.bundle)
        for _, patch in self.patches.values():
            write_fxp(host.load(patch), Path(directory) / f'{patch.name}.fxp', patch.name)
