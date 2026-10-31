#!/usr/bin/env node
// Development-only Thai catalog annotation; no segmentation library ships.
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const segmenter = new Intl.Segmenter('th', { granularity: 'word' });
const thaiEnd = /[\u0e00-\u0e7f]$/u;
const thaiStart = /^[\u0e00-\u0e7f]/u;

export function annotate(text) {
  const words = [...segmenter.segment(text.replaceAll('\u200b', ''))].map((part) => part.segment);
  return words.map((word, index) =>
    word + (thaiEnd.test(word) && thaiStart.test(words[index + 1] ?? '') ? '\u200b' : ''),
  ).join('');
}

export function catalog(text) {
  const lines = text.split('\n');
  if (lines.at(-1) !== '' || (lines.length - 1) % 2 !== 0)
    throw new Error('Expected alternating key/value lines and a final newline');
  for (let index = 0; index < lines.length - 1; index += 2) {
    if (!/^\[[^\r\n]+\]$/.test(lines[index])) throw new Error('Invalid catalog key');
    lines[index + 1] = annotate(lines[index + 1]);
  }
  return lines.join('\n');
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const [input, output] = process.argv.slice(2);
  if (!input || !output || resolve(input) === resolve(output))
    throw new Error('Usage: node thai_breaks.mjs INPUT OUTPUT (distinct files; review output before replacing input)');
  writeFileSync(output, catalog(readFileSync(input, 'utf8')));
  console.log(JSON.stringify({ locale: 'th', node: process.version, icu: process.versions.icu, output }));
}
