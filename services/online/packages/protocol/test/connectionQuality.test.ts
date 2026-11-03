// The shared connection-quality table: its ratings, its formatting and the JSON
// copy the C++ game checks itself against (fixtures/connection-quality.json).
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import {
  CONNECTION_METRICS,
  CONNECTION_QUALITY_CASES,
  CONNECTION_RATING_LABELS,
  formatConnectionValue,
  rateConnection,
  worstRating,
} from '../src/index.ts';

const fixture = JSON.parse(
  readFileSync(
    join(dirname(fileURLToPath(import.meta.url)), '..', 'fixtures', 'connection-quality.json'),
    'utf8',
  ),
) as {
  metrics: typeof CONNECTION_METRICS;
  ratings: typeof CONNECTION_RATING_LABELS;
  cases: typeof CONNECTION_QUALITY_CASES;
};

describe('connection quality', () => {
  it('rates each metric at its thresholds (inclusive lower bounds)', () => {
    expect(rateConnection('ping', 149)).toBe('good');
    expect(rateConnection('ping', 150)).toBe('fair');
    expect(rateConnection('ping', 300)).toBe('poor');
    expect(rateConnection('delay', 171)).toBe('good');
    expect(rateConnection('delay', 400)).toBe('poor');
    expect(rateConnection('behind', 760)).toBe('good');
    expect(rateConnection('behind', 1000)).toBe('fair');
    expect(rateConnection('behind', 2000)).toBe('poor');
  });

  it('orders thresholds and writes units', () => {
    for (const m of Object.values(CONNECTION_METRICS)) expect(m.fairMs).toBeLessThan(m.poorMs);
    expect(formatConnectionValue('ping', 41.6)).toBe('42 ms');
    expect(formatConnectionValue('behind', 1400)).toBe('1.4 s');
    expect(formatConnectionValue('behind', 12_400)).toBe('12 s');
  });

  it('takes the worst rating', () => {
    expect(worstRating()).toBe('good');
    expect(worstRating('good', 'fair')).toBe('fair');
    expect(worstRating('fair', 'poor', 'good')).toBe('poor');
  });

  it('is the table in fixtures/connection-quality.json, which the C++ test reads', () => {
    expect(fixture.metrics).toEqual(CONNECTION_METRICS);
    expect(fixture.ratings).toEqual(CONNECTION_RATING_LABELS);
    expect(fixture.cases).toEqual(CONNECTION_QUALITY_CASES);
    for (const c of fixture.cases) {
      expect(rateConnection(c.metric, c.valueMs), `${c.metric} ${c.valueMs}`).toBe(c.rating);
      expect(formatConnectionValue(c.metric, c.valueMs)).toBe(c.text);
    }
  });
});
