# SPDX-License-Identifier: GPL-3.0-or-later
"""ACE-Step 1.5 text-to-music generation, pinned, cached and optional.

Pipeline role: a ``method = "generated"`` set starts from audio a model produced from
a text prompt. This module turns a ``Request`` (prompt, tempo, key, length, seed)
into a WAV file and is the only place the pipeline touches ACE-Step. Everything after
generation (stems, loop cut, mood mixes) uses the ordinary ``glob2music.adapt`` tools,
exactly like an adapted set.

Why a subprocess: ACE-Step pins its own torch / transformers stack through a uv
lockfile that conflicts with the pipeline's venv, so it runs from its own checkout and
venv (``prepare_checkout`` builds both). The pipeline side needs only the standard
library, and nothing here is imported unless a recipe asks for a generation.

API::

    from glob2music.genai import acestep
    req = acestep.Request(caption='...', bpm=96, keyscale='G minor', duration=110, seed=11)
    wav = acestep.generate(req, ctx.cache_dir, expected_sha256='...', logger=ctx.log,
                           allow_mismatch=ctx.allow_regenerate)

``generate`` first looks in ``<cache_dir>/acestep/<request key>.wav`` and compares the
file's SHA-256 with ``expected_sha256``.

Generation is *not reproducible*, not even on the same hardware: rerunning
orchestral-dawn's request with the same seed, code, weights and GPU (RTX 2070 SUPER)
produced a different piece (waveform correlation 0.14, frame chroma correlation 0.31
with the original), because the LM planner's sampling is not fully seeded; other GPUs
and drivers add their own numerical differences. A regenerated file is therefore a
*new* piece in the same style, whose loop points and mix settings no longer fit.
So the approved generation WAV is the real source of a generated set. It is too large
to commit, and maintainers hold it locally; to rebuild the shipped mix, place it in
the cache under the request key (``cache_path``).

Because of that, a hash mismatch is an error, and on a cache miss ``generate`` refuses
to run the model when an ``expected_sha256`` is recorded. Only an explicit opt-in
(``allow_mismatch=True``, which recipes take from ``build --allow-regenerate`` via
``ctx.allow_regenerate``) runs the model, which needs a prepared checkout
(``GLOB2MUSIC_ACESTEP_DIR`` or the ``checkout`` argument) and a CUDA GPU, and accepts
the different piece with a logged warning. Such a build is unreviewed and must be
listened to and re-approved (with a new hash) before it ships.

Pins (all verified by ``prepare_checkout``):

* code: https://github.com/ace-step/ACE-Step-1.5 at ``ACESTEP_COMMIT`` (MIT);
* weights: Hugging Face ``ACE-Step/Ace-Step1.5`` at ``WEIGHTS_REVISION`` (MIT), each
  file in ``WEIGHTS`` checked by SHA-256.

Local patches (``acestep-turing.patch``, applied by ``prepare_checkout``), both needed
on 8 GB pre-Ampere GPUs such as the RTX 2070 SUPER used for orchestral-dawn:

1. **Load order** (``acestep/llm_inference.py``): the PyTorch LM backend moved the
   model to the GPU *before* casting it, so the 1.7B LM briefly sat on the GPU in
   float32 (about 6.8 GB) and ran out of memory. The patch casts first, then moves.
2. **bf16 override** (``init_service_orchestrator.py``): ACE-Step picks float16 for
   the DiT on GPUs without native bfloat16. The turbo model is fine in float16, but
   the base model produced all-NaN latents. ``ACESTEP_FORCE_DTYPE=bfloat16``
   (emulated on Turing) fixes that. The patch only adds the environment override;
   with it unset, behaviour is unchanged (orchestral-dawn was generated without it).

Licence notes: ACE-Step's code and weights are MIT, and its model card states that
outputs may be used commercially. The authors describe the training data as
licensed, royalty-free or synthetic, but the data is not published. Sets made with
this module must set ``ai_generated = true`` and carry an ``[ai]`` disclosure.
"""
from dataclasses import asdict, dataclass
import hashlib
import json
import logging
import os
from pathlib import Path
import shutil
import subprocess
import sys

log = logging.getLogger('glob2music.genai.acestep')

ACESTEP_REPO = 'https://github.com/ace-step/ACE-Step-1.5.git'
ACESTEP_COMMIT = 'ca1e85fe9430179831e6bc6be790c332190a3866'
WEIGHTS_REPO = 'ACE-Step/Ace-Step1.5'
WEIGHTS_REVISION = '19671f406d603126926c1b7e2adc169acbcade22'

#: SHA-256 of every weight file the turbo DiT + 1.7B LM path loads, relative to the
#: checkout's ``checkpoints/`` directory (values from the Hub's LFS metadata).
WEIGHTS = {
    'acestep-v15-turbo/model.safetensors': '3f6e0797fad420a39bd33979eb6e840e30989e34a3794e843d23b60ec6e422d7',
    'acestep-v15-turbo/silence_latent.pt': 'a778e9dd942f5e8b2c09c55370782d318834432b03dabbcdf70e6ed49ad6358b',
    'vae/diffusion_pytorch_model.safetensors': 'da17edb604c40deaf09e9b24974e590d1ca83a374070e5d0884cfa4bed9a99b0',
    'Qwen3-Embedding-0.6B/model.safetensors': '0437e45c94563b09e13cb7a64478fc406947a93cb34a7e05870fc8dcd48e23fd',
    'Qwen3-Embedding-0.6B/tokenizer.json': 'def76fb086971c7867b829c23a26261e38d9d74e02139253b38aeb9df8b4b50a',
    'acestep-5Hz-lm-1.7B/model.safetensors': 'f161689da73e5ecefa28ff780d51c2d92a00f056d021d7933c779ed5c6cd7db8',
    'acestep-5Hz-lm-1.7B/tokenizer.json': '35af56c3f5cb3ea2cc578aa28a8937770981d504f183ac5c8c38baf4bbd4af4d',
}

PATCH = Path(__file__).with_name('acestep-turing.patch')


class GenAIUnavailable(RuntimeError):
    """A generation is not cached and no usable ACE-Step checkout is configured, or
    the caller did not opt in to regenerating it."""


class GenerationMismatch(RuntimeError):
    """A generation's SHA-256 differs from the approved one (see the module notes)."""


@dataclass(frozen=True)
class Request:
    """One text-to-music generation; every field affects the audio.

    Defaults are the settings orchestral-dawn was made with: the turbo DiT (8 ODE
    steps, timestep shift 3, as ACE-Step recommends for turbo) planned by the 1.7B
    "5 Hz" LM with chain-of-thought ("thinking") on. Turbo ignores guidance scale.
    """

    caption: str
    bpm: int
    keyscale: str
    duration: float
    seed: int
    timesignature: str = '4'
    lyrics: str = '[Instrumental]'
    instrumental: bool = True
    inference_steps: int = 8
    shift: float = 3.0
    thinking: bool = True
    dit: str = 'acestep-v15-turbo'
    lm: str = 'acestep-5Hz-lm-1.7B'

    def key(self):
        """Cache key: a hash of every field plus the pinned code and weights."""
        blob = json.dumps([asdict(self), ACESTEP_COMMIT, WEIGHTS_REVISION], sort_keys=True)
        return hashlib.sha256(blob.encode()).hexdigest()[:20]

    def generation_params(self):
        """Keyword arguments for ``acestep.inference.GenerationParams``."""
        p = asdict(self)
        for k in ('dit', 'lm'):
            p.pop(k)
        return p


def sha256_file(path, chunk=1 << 22):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while block := f.read(chunk):
            h.update(block)
    return h.hexdigest()


def cache_path(request, cache_dir):
    """Where ``generate`` keeps (and looks for) the WAV of ``request``."""
    return Path(cache_dir) / 'acestep' / f'{request.key()}.wav'


def _check_hash(path, expected, logger, allow_mismatch=False):
    digest = sha256_file(path)
    if expected and digest != expected.lower():
        message = (f'{path}: SHA-256 {digest} differs from the approved {expected}. ACE-Step generation is not '
                   'reproducible (the LM planner is not fully seeded), so this is a different piece and the '
                   "recipe's loop points will not fit it. Put the approved generation, which maintainers hold "
                   'locally, at this path to reproduce the shipped set')
        if not allow_mismatch:
            raise GenerationMismatch(message + ', or build with --allow-regenerate to accept a new, unreviewed '
                                     'piece.')
        logger.warning('%s; continuing because regeneration was allowed.', message)
    return digest


# ----------------------------------------------------------------------------- the runner

#: Executed with the checkout's own Python (cwd = checkout), with the settings
#: orchestral-dawn was generated with: DiT on the first GPU with VAE/text-encoder CPU
#: offload, LM on the second GPU when there is one, in bfloat16 with the plain PyTorch
#: backend.
_RUNNER = r'''
import json, os, shutil, sys, torch
from acestep.handler import AceStepHandler
from acestep.llm_inference import LLMHandler
from acestep.inference import GenerationParams, GenerationConfig, generate_music
job = json.loads(sys.argv[1])
root = os.getcwd()
dit = AceStepHandler()
msg, ok = dit.initialize_service(project_root=root, config_path=job['dit'], device='cuda', offload_to_cpu=True)
if not ok: sys.exit('DiT init failed: ' + msg)
lm_device = 'cuda:1' if torch.cuda.device_count() > 1 else 'cuda:0'
llm = LLMHandler()
if job['thinking']:
    msg, ok = llm.initialize(checkpoint_dir=os.path.join(root, 'checkpoints'), lm_model_path=job['lm'],
                             backend='pt', device=lm_device, offload_to_cpu=False, dtype=torch.bfloat16)
    if not ok: sys.exit('LM init failed: ' + msg)
params = GenerationParams(**job['params'])
config = GenerationConfig(batch_size=1, use_random_seed=False, seeds=[job['params']['seed']], audio_format='wav')
result = generate_music(dit, llm if job['thinking'] else None, params, config, save_dir=job['tmp'])
if not result.success: sys.exit('generation failed: %s %s' % (result.error, result.status_message))
shutil.move(result.audios[0]['path'], job['out'])
'''


def _venv_python(checkout):
    return Path(checkout) / '.venv' / 'bin' / 'python'


def generate(request, cache_dir, expected_sha256=None, checkout=None, logger=None, allow_mismatch=False):
    """Path of the WAV (48 kHz 16-bit stereo, ACE-Step's output) for ``request``.

    Cached generations are reused. The file is hashed and compared with
    ``expected_sha256``; a mismatch raises ``GenerationMismatch`` unless
    ``allow_mismatch``. On a cache miss with a recorded ``expected_sha256``, the model
    runs only with ``allow_mismatch`` (it cannot reproduce the approved file; see the
    module notes), from ``checkout`` (default ``$GLOB2MUSIC_ACESTEP_DIR``), which must
    have been set up by ``prepare_checkout``. Raises ``GenAIUnavailable`` otherwise.
    """
    logger = logger or log
    out = cache_path(request, cache_dir)
    if out.exists():
        logger.info('ACE-Step generation from cache: %s', out)
        _check_hash(out, expected_sha256, logger, allow_mismatch)
        return out
    if expected_sha256 and not allow_mismatch:
        raise GenAIUnavailable(
            f'the approved generation (SHA-256 {expected_sha256}) is not at {out}. Maintainers hold it '
            f'locally; copy it there to rebuild the shipped set. Regenerating cannot reproduce it (not even '
            f'on the same GPU); build with --allow-regenerate to make a new, unreviewed piece instead.')
    checkout = checkout or os.environ.get('GLOB2MUSIC_ACESTEP_DIR')
    if not checkout or not _venv_python(checkout).exists():
        raise GenAIUnavailable(
            f'no cached generation at {out}, and no ACE-Step checkout to make one. Either copy the '
            f'original generation (SHA-256 {expected_sha256}) there, or set GLOB2MUSIC_ACESTEP_DIR to a '
            f'checkout made with glob2music.genai.acestep.prepare_checkout (needs a CUDA GPU).')
    verify_weights(checkout)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.parent / f'{request.key()}.tmp'
    tmp.mkdir(exist_ok=True)
    job = {'dit': request.dit, 'lm': request.lm, 'thinking': request.thinking,
           'params': request.generation_params(), 'tmp': str(tmp), 'out': str(out.with_suffix('.part.wav'))}
    logger.info('generating %.0f s with ACE-Step 1.5 (seed %d); about a minute on an RTX 2070 SUPER',
                request.duration, request.seed)
    env = {**os.environ, 'PYTORCH_ALLOC_CONF': 'expandable_segments:True'}
    subprocess.run([str(_venv_python(checkout)), '-c', _RUNNER, json.dumps(job)], cwd=checkout, env=env, check=True)
    Path(job['out']).replace(out)
    shutil.rmtree(tmp, ignore_errors=True)
    _check_hash(out, expected_sha256, logger, allow_mismatch)
    return out


# ----------------------------------------------------------------------------- setup

def verify_weights(checkout):
    """Check every pinned weight file under ``<checkout>/checkpoints`` by SHA-256.

    A successful check is remembered in ``checkpoints/.glob2music-verified`` (with the
    files' sizes and modification times), so the ~11 GB are hashed only once.
    """
    root = Path(checkout) / 'checkpoints'
    stamp = root / '.glob2music-verified'
    state = {rel: [(root / rel).stat().st_size, (root / rel).stat().st_mtime_ns]
             for rel in WEIGHTS if (root / rel).exists()}
    if len(state) == len(WEIGHTS) and stamp.exists() and json.loads(stamp.read_text()) == state:
        return
    for rel, digest in WEIGHTS.items():
        path = root / rel
        if not path.exists():
            raise GenAIUnavailable(f'{path}: missing ACE-Step weight file')
        if sha256_file(path) != digest:
            raise GenAIUnavailable(f'{path}: SHA-256 does not match the pinned weights; re-download it')
    stamp.write_text(json.dumps(state))


def prepare_checkout(dest):
    """Clone ACE-Step at the pinned commit, apply the Turing patch, create its venv and
    download the pinned weights. Needs git, uv and about 25 GB. Run once::

        python3 -c "from glob2music.genai import acestep; acestep.prepare_checkout('/path/ace')"
        export GLOB2MUSIC_ACESTEP_DIR=/path/ace
    """
    dest = Path(dest)
    for tool in ('git', 'uv'):
        if not shutil.which(tool):
            raise GenAIUnavailable(f'{tool} is required to set up ACE-Step')
    if not (dest / '.git').exists():
        subprocess.run(['git', 'clone', ACESTEP_REPO, str(dest)], check=True)
    subprocess.run(['git', '-C', str(dest), 'checkout', '--force', ACESTEP_COMMIT], check=True)
    subprocess.run(['git', '-C', str(dest), 'apply', str(PATCH)], check=True)
    subprocess.run(['uv', 'sync'], cwd=dest, check=True)
    # Plain HTTP downloads: the xet transfer backend has stalled mid-file.
    script = ('from huggingface_hub import snapshot_download as s; '
              f's({WEIGHTS_REPO!r}, revision={WEIGHTS_REVISION!r}, local_dir="checkpoints")')
    subprocess.run([str(_venv_python(dest)), '-c', script], cwd=dest, check=True,
                   env={**os.environ, 'HF_HUB_DISABLE_XET': '1', 'HF_HUB_DISABLE_TELEMETRY': '1'})
    verify_weights(dest)
    return dest


if __name__ == '__main__':      # python3 -m glob2music.genai.acestep DEST
    logging.basicConfig(level=logging.INFO)
    prepare_checkout(sys.argv[1])
