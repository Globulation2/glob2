# Hidden Oasis evidence

- `CONTROL_STUDY_TABLES.md`: every control's measured effect, one control at a time (975 maps).
- `random-requests.jsonl`: the 2,000 random requests, one row each (request, outcome, seconds, walks).
- `run_study.py`, `analyse_study.py`: the driver and tables as run on 2026-09-17 (they expect a copy of
  the client beside them). The reusable version is
  `.agents/skills/glob2-map-design/scripts/control_study.py`.
- `AI_GAMES.txt`: per-colony counters for the last two rounds of headless games, from
  `analyse_games.py` (reusable version: `.agents/skills/glob2-map-design/scripts/game_counters.py`).

See [Hidden Oasis](../../HIDDEN_OASIS.md) for what they show.
