import { test } from 'node:test';
import assert from 'node:assert/strict';
import { annotate, catalog } from '../tools/localization/thai_breaks.mjs';

test('dictionary boundaries keep Thai words and combining marks intact', () => {
  assert.equal(annotate('ภาษาไทย'), 'ภาษา\u200bไทย');
  const text = 'ผู้เล่นกำลังก่อสร้าง %0 / 100 Glob2';
  const result = annotate(text);
  assert.equal(result.replaceAll('\u200b', ''), text);
  assert.ok(!/\u200b[\u0e31\u0e34-\u0e3a\u0e47-\u0e4e]/u.test(result));
  assert.equal(annotate(result), result);
});

test('catalog annotations preserve keys, blank values, technical text and paragraph contracts', () => {
  const input = '[language]\nภาษาไทย\n[blank]\n\n[profile]\nหัวข้อ: ภาษาไทย\\n\\nCPU: %0 {count} https://example.com\n';
  const output = catalog(input);
  assert.equal(output.replaceAll('\u200b', ''), input);
  assert.throws(() => catalog('[missing value]\n'));
});
