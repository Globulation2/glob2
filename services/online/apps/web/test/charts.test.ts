import { describe, expect, it } from 'vitest';
import { niceTicks } from '../src/components/LineChart.tsx';
import { statisticLabel } from '../src/pages/Match.tsx';

describe('chart ticks', () => {
  it('uses whole, distinct steps on count axes', () => {
    // A 0–1 prestige axis showed "0, 1, 1" and units 0–8.4 showed "0, 3, 5, 8".
    expect(niceTicks(0, 1.05, 4, true)).toEqual([0, 1]);
    expect(niceTicks(0, 8.4, 4, true)).toEqual([0, 2, 4, 6, 8]);
    const big = niceTicks(0, 2300, 4, true);
    expect(new Set(big).size).toBe(big.length);
  });
  it('keeps fractional steps for continuous axes', () => {
    expect(niceTicks(0, 10, 4)).toEqual([0, 2.5, 5, 7.5, 10]);
  });
});

describe('statistic names', () => {
  it('turns unknown keys into sentence case', () => {
    expect(statisticLabel('needFoodCritical')).toBe('Need food critical');
    expect(statisticLabel('total_hp')).toBe('Total hp');
  });
});
