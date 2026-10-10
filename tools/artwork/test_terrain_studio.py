# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from PIL import Image, ImageDraw
from terrain_studio import frames, process


class TerrainStudioArtwork(unittest.TestCase):
    def test_terrain_shared_borders_and_animation(self):
        source = Image.new('RGBA', (128, 128), (80, 120, 64, 255))
        draw = ImageDraw.Draw(source)
        draw.rectangle((20, 20, 40, 40), fill=(120, 80, 60, 255))
        tiles = frames(source, 'terrain')
        for tile in tiles:
            for i in range(32):
                self.assertEqual(tile.getpixel((0, i)), tiles[0].getpixel((0, i)))
                self.assertEqual(tile.getpixel((0, i)), tile.getpixel((31, i)))
        out = process(source, 'terrain', 4)
        self.assertEqual(out.size, (128, 128))
        self.assertEqual(out.getextrema()[3], (255, 255))
        self.assertEqual(out.tobytes(), process(source, 'terrain', 4).tobytes())

    def test_resource_anchor_and_stages(self):
        source = Image.new('RGBA', (256, 384))
        draw = ImageDraw.Draw(source)
        for y in range(3):
            for x in range(2):
                draw.ellipse((x*128+40, y*128+60-y*10, x*128+88, y*128+100), fill=(80, 120, 64, 255))
        out = process(source, 'resource', 2)
        self.assertEqual(out.size, (384, 128))
        for x in range(6):
            bbox = out.crop((64*x, 0, 64*(x+1), 64)).getchannel('A').getbbox()
            self.assertEqual(bbox[3], 62)
            self.assertLess(abs((bbox[0]+bbox[2])/2-32), 1)

    def test_nonuniform_rows_preserve_all_six_objects_and_common_scale(self):
        source = Image.new('RGBA', (256, 384))
        draw = ImageDraw.Draw(source)
        for y, (top, bottom) in enumerate([(30, 90), (120, 190), (225, 355)]):
            for x in range(2):
                draw.rectangle((x*128+40, top, x*128+88, bottom), fill=(80, 120, 64, 255))
        tiles = frames(source, 'resource')
        boxes = [tile.getchannel('A').getbbox() for tile in tiles]
        self.assertEqual(len(boxes), 6)
        self.assertTrue(all(box[3] == 62 for box in boxes))
        self.assertEqual(len(set(box[2]-box[0] for box in boxes)), 1)
        self.assertGreater(boxes[4][3]-boxes[4][1], boxes[2][3]-boxes[2][1])
        self.assertEqual(process(source, 'resource').tobytes(), process(source, 'resource').tobytes())

    def test_wrong_three_column_grid_is_not_reinterpreted_as_two(self):
        source = Image.new('RGBA', (256, 384))
        draw = ImageDraw.Draw(source)
        for y in range(3):
            for x in range(3):
                draw.rectangle((x*85+25, y*128+35, x*85+65, y*128+95), fill=(80, 120, 64, 255))
        with self.assertRaisesRegex(ValueError, 'edge'):
            frames(source, 'resource')

    def test_rejects_opaque_or_clipped_resources(self):
        with self.assertRaisesRegex(ValueError, 'transparent'):
            frames(Image.new('RGBA', (128, 192), (10, 20, 30, 255)), 'resource')
        source = Image.new('RGBA', (128, 192))
        ImageDraw.Draw(source).rectangle((0, 0, 20, 20), fill=(10, 20, 30, 255))
        with self.assertRaisesRegex(ValueError, 'edge'):
            frames(source, 'resource')

    def test_rejects_transparent_terrain_and_invalid_animation(self):
        with self.assertRaisesRegex(ValueError, 'opaque'):
            frames(Image.new('RGBA', (128, 128)), 'terrain')
        with self.assertRaisesRegex(ValueError, 'phases'):
            process(Image.new('RGBA', (128, 128)), 'terrain', 5)


if __name__ == '__main__':
    unittest.main()
