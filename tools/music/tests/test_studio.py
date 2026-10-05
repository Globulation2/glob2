# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile
from glob2music.score import load_composition
from glob2music.studio.score import export, load, timeline_id
from glob2music.studio.palettes import instruments

ROOT = Path(__file__).resolve().parents[1]


class StudioBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = export(load_composition(ROOT / "sets/moss-lanterns/composition.py"))
        cls.palette = instruments("acoustic-v1")

    def encoded(self, data):
        return json.dumps(
            data, default=lambda x: sorted(x) if isinstance(x, set) else x
        )

    def test_successful_local_compositions_survive_data_boundary(self):
        for name in (
            "moss-lanterns",
            "thistle-waltz",
            "bramble-jig",
            "fennel-mist",
            "glass-garden",
        ):
            original = load_composition(ROOT / "sets" / name / "composition.py")
            pipeline = "synth-v1" if name == "glass-garden" else "acoustic-v1"
            decoded = load(self.encoded(export(original)), instruments(pipeline))
            self.assertEqual(
                decoded.SCORE.loop_frames(48000), original.SCORE.loop_frames(48000)
            )
            for mood in ("calm", "building", "combat"):
                self.assertEqual(
                    len(decoded.arrange(mood)), len(original.arrange(mood))
                )

    def test_rejects_path_escape_nonfinite_notes_and_unbounded_scores(self):
        for mutate in (
            lambda d: d["parts"]["calm"][0].update(name="../../secrets"),
            lambda d: d["parts"]["calm"][0].update(instrument="unknown"),
            lambda d: d["score"].update(bpm=float("nan")),
            lambda d: d["score"].update(bars=100000),
            lambda d: d["score"].update(accent="execute me"),
            lambda d: d["parts"]["calm"][0]["notes"][0].update(dur=10000),
            lambda d: d.update(mix={"calm": {"lead": 100}}),
            lambda d: d["parts"].pop("combat"),
        ):
            data = copy.deepcopy(self.data)
            mutate(data)
            with self.assertRaises((ValueError, KeyError)):
                load(self.encoded(data), self.palette)

    def test_checks_cannot_be_smuggled_in_as_score_properties(self):
        data = copy.deepcopy(self.data)
        data["score"]["qa"] = {"waivers": {"format": "approved"}}
        with self.assertRaises(ValueError):
            load(self.encoded(data), self.palette)

    def test_rejects_excessive_note_counts_before_rendering(self):
        data = copy.deepcopy(self.data)
        data["parts"]["calm"][0]["notes"] *= 1000
        with self.assertRaises(ValueError):
            load(self.encoded(data), self.palette)

    def test_fresh_data_objects_do_not_execute_generated_hooks(self):
        composition = load(self.encoded(self.data), self.palette)
        self.assertIsNone(composition.SCORE.accent)
        self.assertFalse(hasattr(composition, "perform_part"))

    def test_comparison_timeline_tracks_form_and_tempo_not_melody(self):
        original = load(self.encoded(self.data), self.palette)
        changed = copy.deepcopy(self.data)
        changed["score"]["melody"][0]["pitches"] = [60]
        self.assertEqual(
            timeline_id(original),
            timeline_id(load(self.encoded(changed), self.palette)),
        )
        changed["score"]["bpm"] += 1
        self.assertNotEqual(
            timeline_id(original),
            timeline_id(load(self.encoded(changed), self.palette)),
        )

    def test_seed_controls_composition_import_and_arrangement(self):
        source = (ROOT / "sets/moss-lanterns/composition.py").read_text()
        source += "\nimport random\nimport numpy as np\nSCORE.intensity[0] = random.random()\nSCORE.intensity[1] = float(np.random.random())\n"
        with tempfile.TemporaryDirectory() as directory:
            job = Path(directory)
            (job / "composition.py").write_text(source)

            def run(seed):
                subprocess.run(
                    [
                        sys.executable,
                        "-m",
                        "glob2music.studio",
                        "export",
                        "--job",
                        directory,
                        "--seed",
                        str(seed),
                    ],
                    check=True,
                )
                return (job / "score.json").read_text()

            self.assertEqual(run(7), run(7))
            self.assertNotEqual(run(7), run(8))
