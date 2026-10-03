// Why a match did or did not change ratings, as a sentence for players. The worker
// stores a short reason code in matches.rating_note (see apps/worker/src/ratings);
// API responses carry the sentence, never the code.

const NOTES: Readonly<Record<string, string>> = {
  room: 'Room matches are not rated.',
  unrated_queue: 'Casual matches are not rated.',
  relay_lost: 'The match was lost with its server, so ratings did not change.',
  verified: 'Ratings changed after the result was checked.',
  abandoned: 'Ratings changed: a player left before the end.',
  draw: 'A draw does not change ratings.',
  mutual_leave: 'Both sides left, so ratings did not change.',
  unresolved: 'The match had no clear winner, so ratings did not change.',
  not_two_sides: 'Only matches between two sides change ratings.',
  deleted_account: 'A player’s account was deleted, so ratings did not change.',
  duplicate_entity: 'The same player held two seats, so ratings did not change.',
  verification_diverged:
    'The players’ games disagreed about what happened, so ratings did not change.',
  verification_unverifiable: 'The result could not be checked, so ratings did not change.',
  verification_not_applicable: 'This match is not checked, so ratings did not change.',
};

/** The sentence for a stored rating note; unknown codes get a general sentence. */
export function ratingNoteText(code: string): string {
  return NOTES[code] ?? 'Ratings did not change for this match.';
}
