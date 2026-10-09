import tempfile
import unittest
from pathlib import Path
from check_entity_random import violations


class OwnershipContract(unittest.TestCase):
    def test_calls_are_rejected_but_comments_and_strings_are_not(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for domain in ('unit', 'building', 'team', 'map/pathfind', 'map/gradient'):
                (root / 'src' / domain).mkdir(parents=True)
            (root / 'src/map/gradient/MapGradientDirection.cpp').write_text('')
            path = root / 'src/unit/Unit.cpp'
            path.write_text('// syncRand();\n/* syncRandEngine(); */\n"syncRand()";\nsyncRand();\n')
            self.assertEqual(list(violations(root)), ['src/unit/Unit.cpp:4: implicit shared RNG'])
            for domain in ('ai', 'scripting', 'engine', 'app', 'map/generator'):
                folder = root / 'src' / domain
                folder.mkdir(parents=True, exist_ok=True)
                other = folder / 'Production.cpp'
                other.write_text('syncRand();')
                self.assertEqual(len(list(violations(root))), 2)
                other.unlink()
            path.write_text('SyncRandScope randomScope(engine);')
            self.assertEqual(len(list(violations(root))), 1)
            path.write_text('game.bindRandom();')
            self.assertEqual(len(list(violations(root))), 1)
            path.write_text('unit.entityRandom.nextU32();')
            self.assertEqual(list(violations(root)), [])
            header = root / 'src/team/Team.h'
            header.write_text('inline unsigned f() { return syncRand(); }')
            self.assertEqual(len(list(violations(root))), 1)
            header.unlink()
            nested = root / 'src/team/rules'
            nested.mkdir()
            (nested / 'Decision.cpp').write_text('syncRand();')
            self.assertEqual(len(list(violations(root))), 1)
            (nested / 'Decision.cpp').unlink()
            (root / 'src/unit/UnitTest.cpp').write_text('syncRand();')
            self.assertEqual(list(violations(root)), [])


if __name__ == '__main__':
    unittest.main()
