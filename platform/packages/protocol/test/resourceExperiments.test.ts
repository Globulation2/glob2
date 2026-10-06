import { describe, expect, it } from 'vitest';
import { MatchSetup, matchSetupProblems, parse, resourceExperimentKeys } from '../src/index.ts';
import { SETUP_SAVE_SHARED } from '../scripts/fixtureCases.ts';

const definitions = [{ key: 'resource-fixture', label: 'Resource fixture', help: 'Enables a custom crop.' }];

describe('resource experiment contracts', () => {
  it('allows declared map resource keys while preserving old setup documents', () => {
    expect(matchSetupProblems(parse(MatchSetup, SETUP_SAVE_SHARED))).toEqual([]);
    const setup = parse(MatchSetup, {
      ...SETUP_SAVE_SHARED,
      resourceExperiments: definitions,
      experiments: ['resource-fixture'],
    });
    expect(matchSetupProblems(setup)).toEqual([]);
    expect(matchSetupProblems({ ...setup, resourceExperiments: undefined })).toEqual(
      expect.arrayContaining([expect.objectContaining({ path: '/experiments/0' })]),
    );
  });

  it('rejects duplicate declarations and oversized UTF-8 metadata', () => {
    expect(() => resourceExperimentKeys([...definitions, ...definitions])).toThrow();
    expect(() => resourceExperimentKeys([{ ...definitions[0]!, key: 'Bad key' }])).toThrow();
    expect(() => resourceExperimentKeys([{ ...definitions[0]!, label: 'é'.repeat(257) }])).toThrow();
    expect(() => resourceExperimentKeys([{ ...definitions[0]!, help: '' }])).toThrow();
  });

  it('rejects extra metadata fields at the schema boundary', () => {
    expect(() => parse(MatchSetup, {
      ...SETUP_SAVE_SHARED,
      resourceExperiments: [{ ...definitions[0]!, script: 'not executable' }],
    })).toThrow();
  });
});
