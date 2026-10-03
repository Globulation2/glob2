// Explicit opt-in live evaluation. Credentials never enter fixtures or results.
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, copyFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { homedir, tmpdir } from 'node:os';
import { spawnSync } from 'node:child_process';
import { loadEnvFile } from 'node:process';
import { createHash } from 'node:crypto';
import { randomUUID } from 'node:crypto';
import { createTestDatabase } from '../../db/test/support.ts';
import { fixture } from '../test/support.ts';
import { Credits } from '../src/credits.ts';
import { Commander, type ModelProvider } from '../src/commander.ts';
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
  writeFileSync(input, JSON.stringify({ operations: sources }));
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
  if (e['wake'] && !wakes.length) return false;
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
      (b: Data) =>
        b['shortType'] === e['createdFamily'] &&
        (e['x'] === undefined || b['x'] === e['x']) &&
        (e['y'] === undefined || b['y'] === e['y']) &&
        (e['createdFamily'] !== 1 || b['x'] !== 10 || b['y'] !== 4),
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
const database = await createTestDatabase();
try {
  for (const model of models) {
    const provider = new OpenAICommander(key, model);
    const records: Data[] = [];
    for (const scenario of suite) {
      const { s, account, sessions, client, lease: initialLease } = await fixture(database.db);
      await new Credits(database.db).adjust(account, randomUUID(), 1000000, 'grant');
      const operations: Data[] = [],
        outputs: Data[] = [],
        scriptResults: boolean[] = [],
        reports: string[] = [];
      const providerModels = new Set<string>();
      let state = engine([]),
        lease = initialLease,
        repairs = 0,
        calls = 0,
        input = 0,
        cachedInput = 0,
        output = 0,
        wakeCalls = 0;
      let driverError: string | undefined,
        driving = false,
        finished = false,
        replanning = false;
      const metered: ModelProvider = {
        step: async (system, messages, signal, progress) => {
          const result = await provider.step(system, messages, signal, progress);
          calls++;
          if (replanning) wakeCalls++;
          input += result.usage.input;
          cachedInput += result.usage.cachedInput;
          output += result.usage.output;
          if (result.providerModel) providerModels.add(result.providerModel);
          if (result.text) reports.push(result.text);
          return result;
        },
      };
      const commander = new Commander(database.db, metered, {
        version: 'eval/2',
        model,
        input: 1,
        cachedInput: 1,
        output: 1,
      });
      const sentWakes = new Set<string>();
      const drive = async () => {
        if (driving || finished) return;
        driving = true;
        try {
          const poll = await sessions.poll(
            s.id,
            client,
            lease,
            Number(state['snapshot']['tick']),
            true,
          );
          lease = poll.lease;
          for (const operation of poll.operations) {
            const tool = parse(HiveTool, operation.request);
            operations.push(operation);
            state = engine(operations);
            const result = state['results'].find((r: Data) => r['operationId'] === operation.id);
            if (!result) throw new Error('Client did not complete dispatched operation');
            const valid = result['status'] === 'completed';
            if (tool.kind === 'execute' || tool.kind === 'install' || tool.kind === 'replace')
              scriptResults.push(valid);
            if (!valid) repairs++;
            if (valid) outputs.push(JSON.parse(result['output']));
            await sessions.result(
              s.id,
              operation.id,
              lease,
              result['status'],
              result['output'],
              result['tick'],
            );
          }
          for (const wake of state['wakes']) {
            const id = JSON.stringify([wake.programId, wake.revision, wake.key, wake.tick]);
            if (!sentWakes.has(id)) {
              sentWakes.add(id);
              await sessions.wake(s.id, { ...wake, eventId: randomUUID() }, lease);
            }
          }
        } catch {
          driverError = 'Client bridge failure';
          commander.shutdown();
        } finally {
          driving = false;
        }
      };
      let timer = setInterval(() => {
        void drive();
      }, 50);
      const start = Date.now();
      try {
        await sessions.command(
          s.id,
          randomUUID(),
          scenario.command,
          ['recurring', 'trigger'].includes(scenario.category),
        );
        await commander.run(s.id);
        clearInterval(timer);
        while (driving) await new Promise((r) => setTimeout(r, 10));
        if (scenario.category === 'recurring') {
          // A manual setting changes while the standing order is alive; the next due step must recover it.
          const building = state['snapshot']['buildings'].find(
            (b: Data) => b.team === 0 && b.shortType === scenario.expected['family'],
          );
          const descriptor =
            scenario.expected['field'] === 'workers'
              ? {
                  type: 'workers',
                  building: { id: building.id, generation: building.generation },
                  workers: 1,
                }
              : {
                  type: 'production',
                  building: { id: building.id, generation: building.generation },
                  ratios: [1, 0, 0],
                };
          operations.push({ manual: descriptor }, { advance: 100 });
          state = engine(operations);
        }
        if (scenario.category === 'trigger') {
          // Trigger conditions are false at installation and become true later.
          operations.push({ addWorkers: 5 });
          for (let tick = 0; tick < 5; tick++) {
            operations.push({ advance: 100 });
            state = engine(operations);
            if (state['wakes'].length) break;
          }
          replanning = true;
          await drive();
          timer = setInterval(() => {
            void drive();
          }, 50);
          await commander.run(s.id);
        }
      } catch {
        driverError = 'Evaluation failure';
      } finally {
        finished = true;
        clearInterval(timer);
        while (driving) await new Promise((r) => setTimeout(r, 10));
        commander.shutdown();
      }
      const reportQuality =
        reports.length > 0 &&
        !reports.some((t) => /JavaScript|stack trace|interpreter|tool call/i.test(t));
      const ok =
        !driverError &&
        completed(scenario, state, outputs, state['standing'], state['wakes']) &&
        (scenario.category !== 'trigger' || wakeCalls > 0) &&
        reportQuality;
      const record = {
        id: scenario.id,
        category: scenario.category,
        firstAttemptValid: scriptResults.length > 0 && scriptResults.every(Boolean),
        validScripts: scriptResults.filter(Boolean).length,
        attemptedScripts: scriptResults.length,
        completed: ok,
        commandWithoutRepair: repairs === 0,
        reportQuality,
        wakeCalls,
        repairs,
        calls,
        latencyMs: Date.now() - start,
        usage: { input, cachedInput, output },
        error: driverError,
        operations,
        outputs,
        reports,
        providerModels: [...providerModels],
      };
      records.push(record);
      writeFileSync(
        join(outputDir, model + '.json'),
        JSON.stringify(
          {
            evaluationVersion: 2,
            harness: 'production-commander-native-client',
            model,
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
          completed: ok,
          repairs,
          wakeCalls,
        }),
      );
    }
  }
} finally {
  await database.drop();
}
