#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only: npm install --prefix artifacts/tabler/tooling @resvg/resvg-js@2.6.2
// NODE_PATH=artifacts/tabler/tooling/node_modules node tools/icons/export_tabler.cjs
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { Resvg } = require('@resvg/resvg-js');
if (require('@resvg/resvg-js/package.json').version !== '2.6.2')
  throw new Error('Use @resvg/resvg-js@2.6.2 for reproducible exports');
const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'content-src/icons/tabler');
const manifest = JSON.parse(fs.readFileSync(path.join(source, 'manifest.json')));
const gui = path.join(root, 'content/data/gui');
// One sprite per raster size (data/gui/tabler-<pixels>-, frame i: manifest icon i),
// so exports pack each into a sheet; tabler.txt names the frames for FrontendUI.
fs.writeFileSync(path.join(gui, 'tabler.txt'), manifest.icons.map((icon) => icon.name + '\n').join(''));
for (const [frame, icon] of manifest.icons.entries()) {
  const bytes = fs.readFileSync(path.join(source, `${icon.name}.svg`));
  if (crypto.createHash('sha256').update(bytes).digest('hex') !== icon.sha256)
    throw new Error(`Source changed: ${icon.name}; update the manifest deliberately`);
  const svg = bytes.toString().replaceAll('currentColor', '#ffffff');
  for (const points of [20, 24]) for (const scale of [1, 2, 3]) {
    const pixels = points * scale;
    const renderer = new Resvg(svg, { fitTo: { mode: 'width', value: pixels } });
    fs.writeFileSync(path.join(gui, `tabler-${pixels}-${frame}.png`), renderer.render().asPng());
  }
}
