-- Draws. The engine reports every team tied for the most prestige as won
-- when the prestige goal is reached or the sudden-death timer runs out, allied
-- or not. A win shared by more than one alliance is recorded as a draw for
-- those teams and their participants (worker ratings/outcome.ts), and is not
-- rated.
ALTER TABLE match_participants
  DROP CONSTRAINT match_participants_outcome_check,
  ADD CONSTRAINT match_participants_outcome_check
    CHECK (outcome IN ('won', 'lost', 'draw', 'unresolved', 'abandoned'));

ALTER TABLE match_team_stats
  DROP CONSTRAINT match_team_stats_outcome_check,
  ADD CONSTRAINT match_team_stats_outcome_check
    CHECK (outcome IN ('won', 'lost', 'draw', 'unresolved', 'abandoned'));
