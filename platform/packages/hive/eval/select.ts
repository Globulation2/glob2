// Reproducible evaluation selection. Keep generated evidence under artifacts/.
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { suite } from './suite.ts';
const directory = resolve(process.argv[2] ?? 'artifacts/hive/eval');
// Standard USD per million tokens; verified against the linked model pages.
// A conservative upper bound prices all non-cached input as cache writes.
const rates = [
  ['gpt-6-luna', 0.1, 0.01, 0.5],
  ['gpt-6.1-sol', 2, 0.1, 10],
  ['gpt-6-astra', 10, 1, 50],
] as const;
const candidates = rates.map(([model, input, cached, output]) => {
  const evidence = JSON.parse(readFileSync(join(directory, model + '.json'), 'utf8')) as {
    evaluationVersion: number;
    harness: string;
    contractSha256: string;
    records: {
      id: string;
      firstAttemptValid: boolean;
      validScripts: number;
      attemptedScripts: number;
      completed: boolean;
      repairs: number;
      latencyMs: number;
      usage: { input: number; cachedInput: number; output: number };
    }[];
  };
  if (evidence.evaluationVersion !== 2 || evidence.harness !== 'production-commander-native-client')
    throw new Error('Legacy smoke evidence is not eligible for model selection: ' + model);
  if (
    evidence.records.length !== suite.length ||
    new Set(evidence.records.map((r) => r.id)).size !== suite.length
  )
    throw new Error('Incomplete evaluation: ' + model);
  const tokens = evidence.records.reduce(
    (a, r) => ({
      input: a.input + r.usage.input,
      cachedInput: a.cachedInput + r.usage.cachedInput,
      output: a.output + r.usage.output,
    }),
    { input: 0, cachedInput: 0, output: 0 },
  );
  const cost =
    ((tokens.input - tokens.cachedInput) * input +
      tokens.cachedInput * cached +
      tokens.output * output) /
    1e6;
  const upper = cost + ((tokens.input - tokens.cachedInput) * input * 0.25) / 1e6;
  const latencies = evidence.records.map((r) => r.latencyMs).sort((a, b) => a - b);
  const valid =
    evidence.records.reduce((n, r) => n + r.validScripts, 0) /
    Math.max(
      1,
      evidence.records.reduce((n, r) => n + r.attemptedScripts, 0),
    );
  const complete = evidence.records.filter((r) => r.completed).length / evidence.records.length;
  return {
    model,
    source: `https://developers.openai.com/api/docs/models/${model}`,
    contractSha256: evidence.contractSha256,
    firstAttemptValidity: valid,
    completion: complete,
    repairs: evidence.records.reduce((a, r) => a + r.repairs, 0),
    estimatedUsd: cost,
    upperEstimatedUsd: upper,
    medianLatencyMs: latencies[Math.floor(latencies.length / 2)],
    qualifies: valid >= 0.9 && complete >= 0.95,
  };
});
if (new Set(candidates.map((c) => c.contractSha256)).size !== 1)
  throw new Error('Candidates used different prompts.');
const qualifying = candidates
  .filter((c) => c.qualifies)
  .sort(
    (a, b) =>
      a.estimatedUsd - b.estimatedUsd || (a.medianLatencyMs ?? 0) - (b.medianLatencyMs ?? 0),
  );
const selected = qualifying[0];
if (!selected) throw new Error('No model met the quality gates.');
if (qualifying[1] && selected.upperEstimatedUsd > qualifying[1].estimatedUsd)
  throw new Error('Resolve cache-write metering before comparing overlapping cost estimates.');
const result = {
  selected: selected.model,
  permissionGate:
    'Requires the independent native/browser boundary tests; these gameplay evaluations are not a security proof.',
  candidates,
};
writeFileSync(join(directory, 'selection.json'), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2));
