import { readFile, readdir } from 'node:fs/promises';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import type { MusicStudio, RequestRow, Delivery } from '@glob2/music-studio';
import type { AgentBlobs } from '@glob2/engine/blobs';
import {
  parse,
  MusicStudioCheck,
  type MusicStudioConfig,
  type MusicStudioSettings,
  type MusicStudioStageId,
} from '@glob2/protocol';
import { Attempts, ProviderUncertain, type MusicProvider } from './provider.ts';
import type { MusicRunner, CandidateReport, Rendered } from './runner.ts';
const hash = (s: string) => createHash('sha256').update(s).digest('hex');
const SYSTEM = `You compose original instrumental music for Globulation 2. Write a developed 50–120 second score on one shared timeline, arranged for calm, building and combat. Warm, cozy, downtempo, spacious, with humanised performance. Develop and vary phrases; never repeat a short loop to fill time. Combat changes orchestration, not just volume. Never copy example melodies. Never claim to have listened to audio.
You can write Python composition code, executed without network in a disposable sandbox. Define SCORE (glob2music.score.Score), arrange(mood) -> list[Part], and optionally MIX_ADJUST. Standard trusted humanisation/mixing is used; custom performance hooks are not supported. The selected instrument palette is fixed; arbitrary dependencies and downloads are unavailable. Do not change QA or add waivers.
Respond ONLY with one JSON object: {"action":"read"|"write"|"check"|"render"|"done", "text":"brief user-facing progress", "value":"..."}. read value is one of guide, example, api, instruments, source, report. write value replaces the complete composition.py (at most 128 KiB). check runs cheap score validation. render runs trusted rendering, mastering and all ten audio checks. done only after a passing render. Use the tools to investigate and repair failures. You have at most three render attempts. When revising, edit the supplied source to implement the user's requested change and preserve other musical choices.`;
interface AgentState {
  source: string;
  step: number;
  renders: number;
  tokens: number;
  history: string[];
  report?: CandidateReport;
  delivery?: Delivery;
  renderedSource?: string;
}
export class Pipeline {
  readonly studio: MusicStudio;
  readonly blobs: AgentBlobs;
  readonly provider: MusicProvider;
  readonly runner: MusicRunner;
  readonly config: MusicStudioConfig;
  readonly musicRoot: string;
  readonly origin: string;
  constructor(
    studio: MusicStudio,
    blobs: AgentBlobs,
    provider: MusicProvider,
    runner: MusicRunner,
    config: MusicStudioConfig,
    musicRoot: string,
    origin: string,
  ) {
    this.studio = studio;
    this.blobs = blobs;
    this.provider = provider;
    this.runner = runner;
    this.config = config;
    this.musicRoot = musicRoot;
    this.origin = origin;
  }
  async tick(signal?: AbortSignal) {
    await this.studio.recoverUncertain();
    const row = await this.studio.claim();
    if (!row) return false;
    const cfg = row.input.config ?? this.config;
    const controller = new AbortController();
    // Time spent queued does not consume the execution budget. The first claim fixes the deadline.
    const end =
      typeof row.checkpoints['deadline'] === 'number'
        ? row.checkpoints['deadline']
        : Date.now() + (cfg.timeoutSeconds ?? 1800) * 1000;
    await this.studio.checkpoint(row, 'processing', { deadline: end });
    const combined = AbortSignal.any([
      controller.signal,
      ...(signal ? [signal] : []),
      AbortSignal.timeout(Math.max(1, end - Date.now())),
    ]);
    const timer = setInterval(() => {
      void this.studio
        .heartbeat(row)
        .then((ok) => {
          if (!ok) controller.abort();
        })
        .catch(() => controller.abort());
    }, 10000);
    try {
      await this.work(row, cfg, combined);
    } catch (error) {
      if (error instanceof ProviderUncertain) {
        const current = await this.studio.request(row.id);
        if (current && !['uncertain', 'ready', 'failed'].includes(current.status))
          await this.studio.checkpoint(row, 'uncertain', {});
      } else {
        await this.studio.finish(
          row,
          undefined,
          error instanceof Error ? error.message.slice(0, 3000) : 'Music generation failed.',
        );
      }
    } finally {
      clearInterval(timer);
    }
    return true;
  }
  private async work(row: RequestRow, cfg: MusicStudioConfig, signal: AbortSignal) {
    const version = await this.sourceVersion();
    if (row.checkpoints['sourceVersion'] && row.checkpoints['sourceVersion'] !== version)
      throw Error('The pipeline source changed during recovery. Retry with the installed version.');
    await this.studio.checkpoint(row, 'processing', { sourceVersion: version });
    const attempts = new Attempts(this.studio, cfg.providerCallsPerDay ?? 1);
    const model = cfg.textModel;
    if (!model) throw Error('Configure a music text model.');
    const guide = await readFile(
      join(this.musicRoot, '../../docs/assets/music-style-guide.md'),
      'utf8',
    );
    if (row.kind === 'chat') {
      const prompt = `You are the Globulation 2 music studio assistant. Discuss original CPU-composed instrumental sets, explain changes clearly and keep a concise rolling brief. Never claim to generate until the user presses Generate. Respond with JSON {"text":"reply","brief":"accumulated design brief"}.\n${guide}\nBrief: ${row.input.brief}\nConversation: ${JSON.stringify(row.input.messages)}`;
      this.budget(prompt, 0, cfg);
      const reply = await attempts.run(row, 'chat', model, { prompt }, () =>
        this.provider.text(model, prompt, Math.min(4000, cfg.maxOutputTokens ?? 4000), signal),
      );
      const value = this.json(reply.text) as { text?: unknown; brief?: unknown };
      if (
        typeof value.text !== 'string' ||
        !value.text.trim() ||
        value.text.length > 16000 ||
        typeof value.brief !== 'string' ||
        value.brief.length > 16000
      )
        throw Error('Assistant returned an invalid discussion.');
      await this.studio.text(row, value.text, 0);
      await this.studio.finish(row, { text: value.text, brief: value.brief });
      return;
    }
    const settings = row.input.settings;
    if (!settings) throw Error('Missing music settings.');
    let state = row.checkpoints['agent'] as AgentState | undefined;
    if (!state) {
      const parent = row.input.parent ? await this.studio.request(row.input.parent) : undefined;
      const parentState = parent?.checkpoints['agent'] as AgentState | undefined;
      if (parent?.checkpoints['sourceVersion'] && parent.checkpoints['sourceVersion'] !== version)
        throw Error('The parent uses a different pipeline source. Start a fresh composition.');
      if (row.input.parent && !parentState?.source)
        throw Error('Parent composition is unavailable.');
      state = { source: parentState?.source ?? '', step: 0, renders: 0, tokens: 0, history: [] };
    }
    const thread = await this.studio.own(row.account_id, row.thread_id);
    const metadata = this.metadata(settings, thread.title);
    const example = settings.pipeline === 'acoustic-v1' ? 'moss-lanterns' : 'glass-garden';
    const exampleText = await readFile(
      join(this.musicRoot, 'sets', example, 'composition.py'),
      'utf8',
    );
    const api = await readFile(join(this.musicRoot, 'glob2music/score/model.py'), 'utf8');
    const palette = await readFile(
      join(
        this.musicRoot,
        settings.pipeline === 'acoustic-v1'
          ? 'glob2music/backends/sfizz.py'
          : 'glob2music/studio/palettes.py',
      ),
      'utf8',
    );
    await this.studio.stage(
      row,
      'prepare',
      'running',
      'Writing a composition for three synchronized moods',
    );
    while (state.step < (cfg.maxCalls ?? 12)) {
      signal.throwIfAborted();
      if (state.delivery && state.renderedSource === hash(state.source)) {
        await this.studio.finish(row, state.delivery);
        return;
      }
      const prompt = `${SYSTEM}\nPipeline: ${settings.pipeline}, seed ${settings.seed}\n${guide}\nAPI:\n${api}\nExample (write original music):\n${exampleText}\nBrief: ${row.input.brief}\nConversation: ${JSON.stringify(row.input.messages)}\nCurrent source:\n${state.source}\nTool history:\n${state.history.slice(-8).join('\n')}\nRemaining calls: ${(cfg.maxCalls ?? 12) - state.step}; remaining renders: ${3 - state.renders}`;
      this.budget(prompt, state.tokens, cfg);
      const step = state.step;
      const reply = await attempts.run(
        row,
        `agent:${step}`,
        model,
        { promptHash: hash(prompt) },
        () => this.provider.text(model, prompt, cfg.maxOutputTokens ?? 16000, signal),
      );
      state.tokens +=
        (reply.usage.input_tokens ?? Buffer.byteLength(prompt)) +
        (reply.usage.output_tokens ?? Buffer.byteLength(reply.text));
      const action = this.json(reply.text) as { action?: unknown; text?: unknown; value?: unknown };
      if (typeof action.text === 'string' && action.text.trim())
        await this.studio.text(row, action.text.slice(0, 16000), state.renders + 1);
      let feedback: string;
      let failureStage: MusicStudioStageId = 'prepare';
      try {
        if (action.action === 'read') {
          const values: Record<string, string> = {
            guide,
            api,
            example: exampleText,
            instruments: palette,
            source: state.source,
            report: JSON.stringify(state.report ?? {}),
          };
          feedback =
            values[String(action.value)] ??
            'Unknown document. Read guide, api, example, instruments, source or report.';
        } else if (action.action === 'write') {
          if (
            typeof action.value !== 'string' ||
            !action.value.trim() ||
            Buffer.byteLength(action.value) > (cfg.maxSourceBytes ?? 128 * 1024)
          )
            throw Error(
              `Write a complete composition.py of at most ${cfg.maxSourceBytes ?? 131072} bytes.`,
            );
          state.source = action.value;
          state.delivery = undefined;
          await this.studio.artifact(row, {
            stage: 'prepare',
            kind: 'source',
            label: `Composition source · edit ${step + 1}`,
            hash: await this.blobs.write(Buffer.from(state.source), 'text/plain'),
          });
          feedback = 'composition.py saved. Check or render it.';
        } else if (action.action === 'check' || action.action === 'render') {
          if (!state.source) throw Error('Write composition.py first.');
          const render = action.action === 'render';
          if (render && state.renders >= 3)
            throw Error('All three render attempts have been used.');
          if (render) state.renders++;
          const attempt = Math.max(1, state.renders);
          await this.studio.stage(row, 'prepare', 'complete');
          if (attempt > 1)
            await this.studio.stage(row, 'repair', 'complete', `Starting candidate ${attempt}`);
          failureStage = 'score';
          await this.studio.stage(row, 'score', 'running', `Candidate ${attempt}`);
          let events = Promise.resolve();
          let eventError: unknown;
          const streamedChecks = new Map<string, CandidateReport['checks'][number]>();
          let rendered: Rendered;
          try {
            rendered = await this.runner.run(
              state.source,
              settings,
              render,
              signal,
              (event) => {
                events = events
                  .then(async () => {
                    if (
                      event['stage'] === 'render' ||
                      event['stage'] === 'master' ||
                      event['stage'] === 'checks'
                    ) {
                      failureStage = event['stage'];
                      const previous =
                        event['stage'] === 'render'
                          ? 'score'
                          : event['stage'] === 'master'
                            ? 'render'
                            : 'master';
                      await this.studio.stage(row, previous, 'complete', `Candidate ${attempt}`);
                      await this.studio.stage(
                        row,
                        event['stage'],
                        'running',
                        String(event['detail'] ?? '').slice(0, 1000),
                      );
                    }
                    if (event['check']) {
                      const check = event['check'] as CandidateReport['checks'][number];
                      const previous = streamedChecks.get(check.name);
                      const merged =
                        event['append'] && previous
                          ? { ...check, measures: [...previous.measures, ...check.measures] }
                          : check;
                      streamedChecks.set(check.name, merged);
                      await this.studio.check(row, this.check(merged, attempt, step));
                    }
                  })
                  .catch((error) => {
                    eventError = error;
                  });
              },
              { ...metadata, id: row.id, origin: this.origin },
            );
          } finally {
            await events;
          }
          if (eventError) throw eventError;
          if (
            Object.values(rendered.files).reduce((n, b) => n + b.length, 0) >
            (cfg.maxOutputBytes ?? 134217728)
          )
            throw Error('The rendered output size budget was exceeded.');
          await this.studio.stage(
            row,
            render ? 'checks' : 'score',
            rendered.report.passed ? 'complete' : 'failed',
            `Candidate ${attempt}`,
          );
          state.report = rendered.report;
          feedback = JSON.stringify(rendered.report.checks).slice(0, 32000);
          await this.saveCandidate(row, rendered, attempt, step);
          if (render) {
            if (rendered.report.passed && rendered.report.result) {
              state.delivery = await this.delivery(row, rendered, settings, attempt, step);
              state.renderedSource = hash(state.source);
            } else
              await this.studio.stage(
                row,
                'repair',
                'running',
                `Candidate ${attempt} needs repairs; ${3 - state.renders} attempts remain`,
              );
          }
        } else if (action.action === 'done')
          feedback = 'A complete passing render is required. Use write/check/render.';
        else feedback = 'Unknown action. Use read, write, check, render or done.';
      } catch (error) {
        if (signal.aborted) throw error;
        feedback = (error instanceof Error ? error.message : String(error)).slice(0, 6000);
        if (action.action === 'render' || action.action === 'check') {
          await this.studio.stage(
            row,
            failureStage,
            'failed',
            'The candidate could not complete this stage. See its execution report.',
          );
          await this.studio.artifact(row, {
            stage: failureStage,
            kind: 'report',
            label: `Candidate ${Math.max(1, state.renders)} execution report`,
            hash: await this.blobs.write(
              Buffer.from(JSON.stringify({ error: feedback, step })),
              'application/json',
            ),
          });
          await this.studio.stage(
            row,
            'repair',
            'running',
            `Repairing candidate ${Math.max(1, state.renders)}`,
          );
        }
      }
      state.history.push(`${action.action}: ${feedback}`);
      state.history = state.history.slice(-8);
      state.step++;
      await this.studio.checkpoint(row, 'processing', { agent: state });
      if (state.renders >= 3 && !state.delivery)
        throw Error('No candidate passed within three render attempts. Your credit was returned.');
    }
    if (state.delivery) {
      await this.studio.finish(row, state.delivery);
      return;
    }
    throw Error('The composition budget was exhausted. Your credit was returned.');
  }
  private async sourceVersion() {
    const digest = createHash('sha256');
    for (const directory of ['glob2music', 'sets']) {
      const paths = (await readdir(join(this.musicRoot, directory), { recursive: true }))
        .filter((p) => /\.(py|lock|toml)$/.test(p))
        .sort();
      for (const path of paths) {
        digest.update(directory + '/' + path + '\0');
        digest.update(await readFile(join(this.musicRoot, directory, path)));
      }
    }
    for (const path of [
      'requirements.txt',
      'requirements-synth.txt',
      '../encode_music.py',
      '../../docs/assets/music-style-guide.md',
    ])
      digest.update(await readFile(join(this.musicRoot, path)));
    return digest.digest('hex');
  }
  private json(text: string): unknown {
    return JSON.parse(
      text
        .trim()
        .replace(/^```(?:json)?\s*/, '')
        .replace(/\s*```$/, ''),
    );
  }
  private budget(prompt: string, used: number, cfg: MusicStudioConfig) {
    // UTF-8 bytes upper-bound input token count, reserving output before dispatch.
    if (
      used + Buffer.byteLength(prompt) + (cfg.maxOutputTokens ?? 16000) >
      (cfg.maxTotalTokens ?? 160000)
    )
      throw Error('The music token budget was exhausted.');
  }
  private check(check: CandidateReport['checks'][number], attempt: number, step: number) {
    return parse(MusicStudioCheck, {
      id: `${step}:${check.name}`,
      label: check.name,
      attempt,
      status: check.status,
      measures: check.measures,
    });
  }
  private async saveCandidate(row: RequestRow, candidate: Rendered, attempt: number, step: number) {
    await this.studio.artifact(row, {
      stage: 'score',
      kind: 'source',
      label: `Candidate ${attempt} score`,
      hash: await this.blobs.write(Buffer.from(candidate.score), 'application/json'),
    });
    for (const c of candidate.report.checks)
      await this.studio.check(row, this.check(c, attempt, step));
    const preview = candidate.files['preview.opus'];
    if (preview)
      await this.studio.artifact(row, {
        stage: 'checks',
        kind: 'preview',
        label: `Candidate ${attempt} · ${candidate.report.passed ? 'validated' : 'needs repairs'}`,
        hash: await this.blobs.write(preview, 'audio/ogg'),
      });
    await this.studio.artifact(row, {
      stage: 'checks',
      kind: 'report',
      label: `Candidate ${attempt} report`,
      hash: await this.blobs.write(
        Buffer.from(JSON.stringify(candidate.report)),
        'application/json',
      ),
    });
  }
  private async delivery(
    row: RequestRow,
    candidate: Rendered,
    settings: MusicStudioSettings,
    attempt: number,
    step: number,
  ): Promise<Delivery> {
    if (!candidate.report.result) throw Error('Missing delivery audio.');
    const mandatory = [
      'score',
      'format',
      'loudness',
      'seam',
      'repetition',
      'alignment',
      'contrast',
      'noise',
      'balance',
      'audibility',
      'dropout',
    ];
    if (
      !mandatory.every((name) =>
        candidate.report.checks.some(
          (c) =>
            c.name === name &&
            !['fail', 'skip', 'waived'].includes(c.status) &&
            c.measures.length > 0 &&
            c.measures.every((m) => !['fail', 'skip', 'waived'].includes(m.status)),
        ),
      )
    )
      throw Error('Incomplete validation cannot deliver music.');
    const assets = [];
    for (const [kind, file] of [
      ['calm', 'a1.opus'],
      ['building', 'a2.opus'],
      ['combat', 'a3.opus'],
      ['waveforms', 'waveforms.json'],
      ['zip', 'set.zip'],
    ] as const) {
      const bytes = candidate.files[file];
      if (!bytes) throw Error('Missing delivery asset.');
      assets.push({
        kind,
        hash: await this.blobs.write(
          bytes,
          kind === 'waveforms'
            ? 'application/json'
            : kind === 'zip'
              ? 'application/zip'
              : 'audio/ogg',
        ),
      });
    }
    const thread = await this.studio.own(row.account_id, row.thread_id);
    return {
      metadata: candidate.report.metadata ?? this.metadata(settings, thread.title),
      result: candidate.report.result,
      assets,
      checks: candidate.report.checks.map((c) => this.check(c, attempt, step)),
    };
  }
  private metadata(settings: MusicStudioSettings, title: string): Delivery['metadata'] {
    return {
      title,
      artist: 'Globulation 2 Music Studio',
      description:
        'An original instrumental soundtrack with synchronized calm, building and combat moods.',
      license: settings.license ?? 'CC-BY-4.0',
      credits:
        settings.pipeline === 'acoustic-v1'
          ? 'AI-composed. VSCO 2 Community Edition / Versilian Community Sample Library (CC0); rendered with sfizz.'
          : 'AI-composed. Surge XT and Globulation 2 DSP; curated patches derived from the CC0 Init patch.',
      sources: [],
      tags: [settings.pipeline === 'acoustic-v1' ? 'acoustic' : 'synth'],
      aiGenerated: true,
    };
  }
}
