// Explicit opt-in live evaluation. Credentials never enter fixtures or results.
import {
  readFileSync,
  writeFileSync,
  mkdirSync,
  mkdtempSync,
  existsSync,
  copyFileSync,
} from 'node:fs';
import { resolve, join } from 'node:path';
import { homedir, tmpdir } from 'node:os';
import { spawnSync } from 'node:child_process';
import { loadEnvFile } from 'node:process';
import { createHash } from 'node:crypto';
import type { ModelMessage } from 'ai';
import { parse, HiveTool, HIVE_API_VERSION } from '@glob2/protocol';
import { OpenAICommander, PROMPT_VERSION, commanderPrompt } from '../src/commander.ts';
import { suite, type Scenario } from './suite.ts';
if (process.env['HIVE_LIVE_EVAL'] !== '1')
  throw new Error('Live evaluations require HIVE_LIVE_EVAL=1.');
if (process.env['HIVE_EVAL_REASONINGCANVAS'] === '1')
  loadEnvFile(join(homedir(), 'reasoningcanvas/server/.env.local'));
const key = process.env['HIVE_OPENAI_API_KEY'] ?? process.env['OPENAI_API_KEY'];
if (!key) throw new Error('An explicitly enabled development credential is required.');
const root = resolve(import.meta.dirname, '../../../..');
const outputDir = resolve(process.env['HIVE_EVAL_OUTPUT'] ?? join(root, 'artifacts/hive/eval'));
mkdirSync(outputDir, { recursive: true });
const binary = resolve(
  process.env['HIVE_EVAL_ENGINE'] ??
    join(root, 'build/linux/client/release/test/glob2-engine-tests'),
);
const temporary = mkdtempSync(join(tmpdir(), 'glob2-hive-eval-'));
const engineBinary = join(temporary, 'engine');
copyFileSync(binary, engineBinary);
const system = commanderPrompt();
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Data = Record<string, any>; // Evaluation consumes deliberately untrusted JSON, checked by the native host.
function engine(sources: unknown[]): Data {
  const input = join(temporary, 'input.json'),
    output = join(temporary, 'output.json');
  writeFileSync(input, JSON.stringify({ sources }));
  const run = spawnSync(
    engineBinary,
    ['--test-case=Hive Mind evaluation gameplay fixture', '--no-colors'],
    {
      cwd: root,
      env: {
        PATH: process.env['PATH'],
        SDL_VIDEODRIVER: 'dummy',
        SDL_AUDIODRIVER: 'dummy',
        GLOB2_HIVE_EVAL_INPUT: input,
        GLOB2_HIVE_EVAL_OUTPUT: output,
      },
      encoding: 'utf8',
      timeout: 30000,
    },
  );
  if (run.status !== 0)
    throw new Error('Evaluation engine failed; inspect its test harness independently.');
  return JSON.parse(readFileSync(output, 'utf8')) as Data;
}
function completed(
  scenario: Scenario,
  state: Data,
  outputs: Data[],
  standing: Data[],
  wakes: Data[],
): boolean {
  const e = scenario.expected;
  if (e['standing'] && !standing.length) return false;
  if (e['wake']) return wakes.some((w) => w['key'] === e['wake']);
  if (e['output'])
    return outputs.some((out) =>
      Object.entries(e['output'] as Data).every(([k, v]) => out?.[k] === v),
    );
  if (e['ownedList'])
    return outputs.some(
      (out) =>
        Array.isArray(out?.['buildings']) &&
        out['buildings'].length === e['ownedList'] &&
        out['buildings'].every((b: Data) => b['team'] === 0 && Number.isInteger(b['generation'])),
    );
  const buildings = state['snapshot']['buildings'].filter((b: Data) => b['team'] === 0);
  if (e['createdFamily'] !== undefined)
    return buildings.some(
      (b: Data) => b['shortType'] === e['createdFamily'] && b['x'] === e['x'] && b['y'] === e['y'],
    );
  const selected = buildings.filter((b: Data) => b['shortType'] === e['family']);
  return (
    selected.length > 0 &&
    selected.every(
      (b: Data) => JSON.stringify(b[e['field'] as string]) === JSON.stringify(e['value']),
    )
  );
}
const models = (process.env['HIVE_EVAL_MODELS'] ?? 'gpt-6-luna,gpt-6.1-sol,gpt-6-astra').split(',');
for (const model of models) {
  const provider = new OpenAICommander(key, model);
  const existing = join(outputDir, model + '.json');
  const prior = existsSync(existing) ? JSON.parse(readFileSync(existing, 'utf8')) : null;
  if (prior && prior.contractSha256 !== createHash('sha256').update(system).digest('hex'))
    throw new Error('Prompt changed; use a fresh evaluation output directory.');
  const records: Data[] = prior?.records ?? [];
  const providerModels = new Set<string>();
  for (const scenario of suite) {
    if (records.some((r) => r['id'] === scenario.id)) continue;
    const messages: ModelMessage[] = [{ role: 'user', content: scenario.command }];
    const sources: string[] = [],
      outputs: Data[] = [],
      standing: Data[] = [],
      wakes: Data[] = [];
    let firstValid: boolean | undefined,
      repairs = 0,
      calls = 0,
      input = 0,
      cachedInput = 0,
      output = 0,
      error: string | undefined;
    let state = engine([]);
    const start = Date.now();
    try {
      for (let round = 0; round < 12; round++) {
        const step = await provider.step(system, messages, AbortSignal.timeout(60000));
        if (step.providerModel) providerModels.add(step.providerModel);
        calls++;
        input += step.usage.input;
        cachedInput += step.usage.cachedInput;
        output += step.usage.output;
        messages.push(...step.messages);
        if (!step.calls.length) break;
        const identities = step.messages.flatMap((m) =>
          m.role === 'assistant' && Array.isArray(m.content)
            ? m.content.filter((c) => c.type === 'tool-call')
            : [],
        );
        for (let index = 0; index < step.calls.length; index++) {
          let result: unknown;
          try {
            const tool = parse(HiveTool, step.calls[index]?.input);
            if (tool.kind === 'execute' || tool.kind === 'install') {
              const source = tool.kind === 'execute' ? tool.source : tool.program.source;
              sources.push(source);
              state = engine(sources);
              const outcome = state['results'].at(-1);
              firstValid ??= !!outcome['ok'];
              if (!outcome['ok']) {
                repairs++;
                sources.pop();
              } else {
                outputs.push(outcome['output']);
                wakes.push(...outcome['wakes']);
                if (tool.kind === 'install') standing.push(tool.program);
              }
              result = outcome['ok'] ? { status: 'completed', output: outcome['output'] } : outcome;
            } else if (tool.kind === 'list') result = standing;
            else
              result = {
                error:
                  'No existing standing order is available for that operation in this fresh scenario.',
              };
          } catch {
            firstValid ??= false;
            repairs++;
            result = { error: 'Invalid operation. Recheck the supported contract.' };
          }
          const identity = identities[index];
          if (!identity) throw new Error('Missing tool identity');
          messages.push({
            role: 'tool',
            content: [
              {
                type: 'tool-result',
                toolCallId: identity.toolCallId,
                toolName: 'colony',
                output: { type: 'json', value: JSON.parse(JSON.stringify(result)) },
              },
            ],
          });
        }
      }
    } catch {
      error = 'Provider or evaluation failure';
    }
    const record = {
      id: scenario.id,
      category: scenario.category,
      firstAttemptValid: firstValid ?? false,
      completed: !error && completed(scenario, state, outputs, standing, wakes),
      repairs,
      calls,
      latencyMs: Date.now() - start,
      usage: { input, cachedInput, output },
      error,
      sources,
      outputs,
    };
    records.push(record);
    writeFileSync(
      join(outputDir, model + '.json'),
      JSON.stringify(
        {
          model,
          providerModels: [...providerModels],
          promptVersion: PROMPT_VERSION,
          apiVersion: HIVE_API_VERSION,
          contractSha256: createHash('sha256').update(system).digest('hex'),
          records,
        },
        null,
        2,
      ),
    );
    console.log(
      JSON.stringify({
        model,
        id: record.id,
        valid: record.firstAttemptValid,
        completed: record.completed,
        repairs,
        latencyMs: record.latencyMs,
      }),
    );
  }
}
