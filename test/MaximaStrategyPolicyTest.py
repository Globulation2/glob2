#!/usr/bin/env python3
"""Structural guardrails for the unified Maxima strategy path and save format."""

import hashlib
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parent.parent


def numeric_inventory(path: Path) -> tuple[int, str]:
    source = path.read_text(encoding="utf-8")
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//.*", "", source)
    source = re.sub(r'"(?:\\.|[^"\\])*"', "", source)
    values = re.findall(
        r"(?<![A-Za-z_])(?:0[xX][0-9A-Fa-f]+|\d+)(?:[uUlL]*)(?![A-Za-z_])",
        source,
    )
    digest = hashlib.sha256("\n".join(sorted(values)).encode()).hexdigest()
    return len(values), digest


class MaximaStrategyPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.maxima = (ROOT / "src/AIMaxima.cpp").read_text()
        cls.game = (ROOT / "src/Game.cpp").read_text()
        cls.strategy = (ROOT / "src/AIMaximaStrategy.cpp").read_text()

    def test_strategy_consumers_do_not_read_external_configuration(self) -> None:
        consumers = "".join(
            (ROOT / name).read_text()
            for name in (
                "src/AIMaxima.cpp",
                "src/AIMaximaFarming.cpp",
                "src/AIMaximaRecon.cpp",
                "src/AIMaximaRuntime.cpp",
            )
        )
        for forbidden in ("getenv(", "ifstream", "ConfigVector", "GLOB2_NICOWAR"):
            self.assertNotIn(forbidden, consumers)
        self.assertIn("resolveMaximaStrategy(", self.maxima)
        self.assertIn("resolvedMaximaStrategies", self.game)

    def test_current_save_omits_configuration_plans_and_phases(self) -> None:
        version = (ROOT / "src/Version.h").read_text()
        self.assertIn("#define VERSION_MINOR 99", version)
        # The relentless offense changes simulation results, so an older client
        # must be refused rather than allowed to desync, and the trimmed Maxima
        # mission state needs its own load gate.
        self.assertIn("versionMinor>=98", self.maxima)
        save = self.maxima[self.maxima.index("void Maxima::save(") :]
        self.assertNotIn('writeText(strategy.getStrategyName()', save)
        self.assertNotIn('writeText(tuning', save)
        self.assertNotIn('writeEnterSection("classic_plan")', save)
        self.assertNotIn('writeEnterSection("dynamic_plan")', save)
        self.assertNotIn('writeUint8(growth_phase', save)
        loader = self.maxima[
            self.maxima.index("bool Maxima::loadDirector") :
            self.maxima.index("bool Maxima::loadState")
        ]
        self.assertIn("versionMinor<=87", loader)
        state = self.maxima[self.maxima.index("bool Maxima::loadState") :]
        self.assertNotIn('readText("strategy_name")', state)
        self.assertIn("development_planner.load(stream", state)
        self.assertIn("loadLegacyState", self.maxima)
        game_io = (ROOT / "src/Game_io.cpp").read_text()
        self.assertNotIn(
            "resolvedMaximaStrategies", game_io[game_io.index("void Game::save(") :]
        )

    def test_maxima_keeps_id_and_legacy_section_names(self) -> None:
        ai_header = (ROOT / "src/ai/AI.h").read_text()
        ai_source = (ROOT / "src/ai/AI.cpp").read_text()
        self.assertRegex(ai_header, r"MAXIMA\s*=\s*7")
        self.assertNotIn("NICOWAR_V2", ai_header + ai_source)
        self.assertNotIn("NICOWAR_V4", ai_header + ai_source)
        # Maxima is a separate implementation alongside Nicowar, not a
        # replacement for it, so it owns its own save sections.
        self.assertIn('readEnterSection("AIMaxima")', self.maxima)
        self.assertIn('writeEnterSection("AIMaxima")', self.maxima)
        self.assertIn('readEnterSection("MaximaState")', self.maxima)
        self.assertIn('writeEnterSection("MaximaState")', self.maxima)

    def test_literal_inventory_requires_explicit_manifest_review(self) -> None:
        manifest = json.loads(
            (ROOT / "test/MaximaLiteralManifest.json").read_text()
        )
        for relative, expected in manifest["sources"].items():
            count, digest = numeric_inventory(ROOT / relative)
            self.assertEqual(count, expected["tokens"], relative)
            self.assertEqual(digest, expected["sha256"], relative)
            self.assertTrue(expected["classifications"], relative)

    def test_schema_and_complete_base_have_exactly_the_same_keys(self) -> None:
        specifications = []
        for line in self.strategy.splitlines():
            if not re.match(r"\s*(?:INT|BOOL)_SPEC\(", line):
                continue
            strings = re.findall(r'"([^"]*)"', line)
            self.assertGreaterEqual(len(strings), 4, line)
            specifications.append((strings[0], strings[-1]))
        schema_keys = [key for key, _description in specifications]
        base = (ROOT / "data/maxima/base.strategy").read_text()
        base_keys = re.findall(r"^([a-z][a-z0-9_.]+)\s*=", base, re.M)
        self.assertEqual(len(schema_keys), len(set(schema_keys)))
        self.assertEqual(len(base_keys), len(set(base_keys)))
        self.assertEqual(set(schema_keys), set(base_keys))
        self.assertTrue(all(description.strip() for _key, description in specifications))

    def test_global_swarm_staffing_caps_are_removed(self) -> None:
        for source in (self.strategy, self.maxima,
                       (ROOT / "src/AIMaximaStrategy.h").read_text(),
                       (ROOT / "data/maxima/base.strategy").read_text()):
            self.assertNotIn("swarm_worker_cap", source)

    def test_every_registered_member_has_a_runtime_consumer(self) -> None:
        consumers = "\n".join(
            path.read_text()
            for path in (ROOT / "src").glob("AIMaxima*.cpp")
            if path.name != "AIMaximaStrategy.cpp"
        )
        unused = []
        for line in self.strategy.splitlines():
            match = re.match(
                r"\s*(?:INT|BOOL)_SPEC\(([^,]+),\s*([^,]+),", line
            )
            if not match:
                continue
            key = re.findall(r'"([^"]*)"', line)[0]
            member = match.group(2).strip()
            if not re.search(rf"\.{re.escape(member)}\b", consumers):
                unused.append(key)
        # Retained so existing strategy overlays remain loadable. These former
        # food/tactical coupling knobs deliberately have no runtime authority.
        self.assertEqual({
            "military.campaign_sustainable_food_percent",
            "emergencies.population_trend_threshold",
            "emergencies.food_trend_threshold",
        }, set(unused))

    def test_every_parameter_has_an_explicit_impact_tier(self) -> None:
        specifications = [
            line for line in self.strategy.splitlines()
            if re.match(r"\s*(?:INT|BOOL)_SPEC\(", line)
        ]
        impacts = [
            re.findall(r"StrategyImpact(Critical|High|Medium|Low)", line)
            for line in specifications
        ]
        # 689 with the gated tactical layer; the relentless offense removed 50
        # muster, casualty, relief and teamplay keys and added three.
        self.assertEqual(643, len(specifications))
        self.assertTrue(all(len(impact) == 1 for impact in impacts))
        self.assertEqual(
            {"Critical", "High", "Medium", "Low"},
            {impact[0] for impact in impacts},
        )

    def test_extracted_decisions_are_strategy_driven(self) -> None:
        for required in (
            "strategy.staffing.construction_inn_workers",
            "strategy.staffing.completed_tower_workers",
            "strategy.economy.first_pool_target",
            "strategy.trends",
            "strategy.placement.enemy_threat_radius",
            "strategy.tactics.failed_target_quarantine_ticks",
            "strategy.scheduling.strategy_interval_ticks",
        ):
            self.assertIn(required, self.maxima)
        for obsolete_literal in (
            "timer%100 == 17",
            "timer%100 == 33",
            "timer%250 == 85",
            "duration<=500",
            "timer+5000",
            "tile.protectedness=map->isGuardArea(x,y,echo.player->team->me)?80:35",
        ):
            self.assertNotIn(obsolete_literal, self.maxima)
        placement = (ROOT / "src/AIMaximaPlacement.cpp").read_text()
        self.assertNotIn("building.level==1?4:8", placement)
        self.assertIn("placementPolicy.upgradeLevel1Workers", placement)

    def test_algae_policy_uses_reachable_units_and_shore_access(self) -> None:
        self.assertIn(
            "known_algae_units>walk_accessible_algae_units", self.maxima
        )
        self.assertIn(
            "accessible_algae_units>=school_algae_requirement()", self.maxima
        )
        self.assertIn(
            "world.accessibleSupplies[ALGA]=accessible_algae_units;", self.maxima
        )
        self.assertIn(
            "result.accessibleAlgaeUnits+=tile.resource.amount;", self.maxima
        )
        self.assertIn(
            "walking[index]=clear && !map->isWater(x, y);", self.maxima
        )
        self.assertIn("intent.requiredResourceType=ALGA;", self.maxima)
        self.assertNotIn(
            "unit->carriedRessource==CORN || unit->carriedRessource==ALGA",
            self.maxima,
        )

    def test_swarm_executor_does_not_override_controller(self) -> None:
        staffing = self.maxima[self.maxima.index("void Maxima::manage_swarm"):
                               self.maxima.index("void Maxima::OffenseDiagnostics::reset")]
        # Staffing is the building's own closed loop, but birth funding stays a
        # colony decision: the executor may consult the budget to pause
        # production and must never derive its own.
        self.assertIn("staff_building(echo, id);", staffing)
        self.assertIn("if(budget.swarm_workers<=0)", staffing)
        self.assertNotIn("SwarmController::plan(", staffing)
        self.assertNotIn("needFood", staffing)
        self.assertNotIn("recovery_active", staffing)
        self.assertIn("SwarmController::plan(", self.maxima)

    def test_maxima_worker_assignments_share_the_engine_limit(self) -> None:
        building = (ROOT / "src/building/Building.h").read_text()
        runtime = (ROOT / "src/AIMaximaRuntime.cpp").read_text()
        gui_header = (ROOT / "src/gui/GameGUI.h").read_text()
        orders = (ROOT / "src/Game_orders.cpp").read_text()
        game_header = (ROOT / "src/Game.h").read_text()
        self.assertIn("static constexpr int MAX_UNIT_WORKING=20;", building)
        self.assertIn(
            "workers>Building::MAX_UNIT_WORKING?Building::MAX_UNIT_WORKING:workers",
            runtime,
        )
        # The engine enforces the same ceiling when it executes the order, under
        # its own name. Both constants are 20; keep them pinned to each other.
        self.assertIn(
            "static constexpr int MAX_BUILDING_WORKER_REQUEST = 20;", game_header
        )
        self.assertIn(
            "omb.numberRequested <= MAX_BUILDING_WORKER_REQUEST", orders
        )
        self.assertNotIn("#define MAX_UNIT_WORKING", gui_header)
