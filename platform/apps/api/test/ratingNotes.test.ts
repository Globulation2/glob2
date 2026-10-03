// The match page explains ratings in words, whatever code the worker stored
// (the rating and intake code lives in @glob2/play).
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { ratingNoteText } from '../src/history/ratingNotes.ts';

const WORKER = join(import.meta.dirname, '../../../packages/play/src/ratings');
const INTAKE = join(import.meta.dirname, '../../../packages/play/src/play/intake.ts');

/** Every rating_note the worker writes: finish(...) reasons, decisions and literals. */
function storedCodes(): string[] {
  // `verification_${match.verification}` for the two verdicts that end unrated.
  const codes = new Set<string>(['verification_diverged', 'verification_unverifiable']);
  const files = [
    ...readdirSync(WORKER)
      .filter((f) => f.endsWith('.ts'))
      .map((f) => join(WORKER, f)),
    INTAKE,
  ];
  for (const file of files) {
    const text = readFileSync(file, 'utf8');
    for (const m of text.matchAll(/rating_note: '([a-z_]+)'/g)) codes.add(m[1]!);
    for (const m of text.matchAll(/finish\(trx, matchId, '[a-z_]+', '([a-z_]+)'/g))
      codes.add(m[1]!);
    for (const m of text.matchAll(/'room' : '([a-z_]+)'/g)) codes.add(m[1]!);
    // Rating decisions: { kind: 'unchanged' | 'apply', reason: 'draw' | ... }
    for (const m of text.matchAll(
      /kind: '(?:unchanged|apply)'[^}]*?reason: ('[a-z_]+'(?: \| '[a-z_]+')*)/g,
    )) {
      for (const code of m[1]!.matchAll(/'([a-z_]+)'/g)) codes.add(code[1]!);
    }
  }
  codes.add('room');
  return [...codes];
}

describe('rating notes', () => {
  it('has a sentence for every code the worker stores', () => {
    const generic = ratingNoteText('no_such_code');
    const codes = storedCodes();
    expect(codes).toEqual(
      expect.arrayContaining(['room', 'unrated_queue', 'draw', 'mutual_leave', 'relay_lost']),
    );
    for (const code of codes) {
      expect(ratingNoteText(code), code).not.toBe(generic);
      expect(ratingNoteText(code), code).toMatch(/^[A-Z].*\.$/);
    }
  });

  it('never shows a code to players', () => {
    expect(ratingNoteText('room')).toBe('Room matches are not rated.');
    expect(ratingNoteText('unrated_queue')).toBe('Casual matches are not rated.');
    expect(ratingNoteText('something_new')).not.toContain('_');
  });
});
