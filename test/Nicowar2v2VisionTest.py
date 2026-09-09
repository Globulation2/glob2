#!/usr/bin/env python3
"""Guard the 2v2 harness against diverging from normal allied vision."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class Nicowar2v2VisionTest(unittest.TestCase):
    def test_worker_uses_the_regular_alliance_vision_setup(self) -> None:
        engine = (ROOT / "src/Engine.cpp").read_text()
        start = engine.index("int Engine::createNicowar2v2TournamentGame")
        end = engine.index("int Engine::createNicowarScenarioGame", start)
        worker = engine[start:end]

        self.assertIn("return initGame(map, game);", worker)
        self.assertNotIn("sharedVisionExchange=", worker)
        self.assertNotIn("sharedVisionFood=", worker)
        self.assertNotIn("sharedVisionOther=", worker)

        game = (ROOT / "src/Game.cpp").read_text()
        start = game.index("void Game::setAlliances")
        end = game.index("bool Game::load", start)
        alliances = game[start:end]
        self.assertIn("teams[i]->sharedVisionOther |= teams[j]->me;", alliances)

    def test_runner_describes_shared_vision(self) -> None:
        runner = (ROOT / "tools/run_nicowar_2v2_tournament.py").read_text()
        self.assertIn("normal allied vision", runner)
        self.assertNotIn("no shared vision", runner)


if __name__ == "__main__":
    unittest.main()
