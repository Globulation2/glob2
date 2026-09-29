#!/usr/bin/env python3
"""Guard the boundary that previously transplanted desktop controls into touch.

Behavior and serialized orders are exercised by GameGUITouchHarness. These checks
specifically prevent reintroducing composed-screen reuse behind those behaviors.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TouchPresentationStructureTest(unittest.TestCase):
    def test_touch_does_not_forward_to_desktop_composition(self):
        source = "\n".join(path.read_text() for path in (ROOT / "src/gui").glob("GameGUITouch*.cpp"))
        for forbidden in ("gui.drawPanel(", "gui.handleMenuClick(", "gui.minimap.",
                          "allocationTab", "panelOrigin(", "PhoneTheme::"):
            self.assertNotIn(forbidden, source)

    def test_editor_owns_palette_and_dialog_composition(self):
        source = "\n".join(path.read_text() for path in (ROOT / "src/map/edit").glob("PhoneEditor*.cpp"))
        for forbidden in ("minimap->", "minimap.", "drawMenu(", "editor.handleClick("):
            self.assertNotIn(forbidden, source)
        for view in ("menuScreen", "teamsEditor", "scriptEditor", "areaName"):
            self.assertIn(f"{view}->drawTouch()", source)
            self.assertIn(f"{view}->eventTouch(event)", source)

    def test_gesture_state_does_not_own_game_rules_or_drawing(self):
        source = (ROOT / "src/gui/TouchInteractionSession.h").read_text()
        for forbidden in ("OrderCreate", "GraphicContext", "BuildingType", "Sprite"):
            self.assertNotIn(forbidden, source)

    def test_placement_has_one_shared_commit_boundary(self):
        source = (ROOT / "src/gui/GameGUITouchPlacement.cpp").read_text()
        self.assertEqual(source.count("gui.toolManager.confirmBuilding("), 1)
        self.assertNotIn("OrderCreate", source)


if __name__ == "__main__":
    unittest.main()
