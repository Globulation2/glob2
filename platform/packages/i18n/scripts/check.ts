import { readFileSync } from 'node:fs';
import { locales } from '../src/locales.ts';
import type { Catalog } from '../src/index.ts';
import { sourceErrors } from './source-contract.ts';
const read = (name: string): Catalog =>
  JSON.parse(readFileSync(new URL(`../locales/${name}.json`, import.meta.url), 'utf8')) as Catalog;
const pluralSources = JSON.parse(
  readFileSync(new URL('../plural-sources.json', import.meta.url), 'utf8'),
) as { singular: string; plural: string }[];
const sharedValues = JSON.parse(
  readFileSync(new URL('../shared-values.json', import.meta.url), 'utf8'),
) as Record<string, string[]>;
const invariants = JSON.parse(
  readFileSync(new URL('../invariants.json', import.meta.url), 'utf8'),
) as Record<string, string[]>;
const english = read('en');
const keys = Object.keys(english).sort();
const placeholders = (text: string) =>
  [...text.matchAll(/\{([a-zA-Z][a-zA-Z0-9_]*)\}/g)]
    .map((m) => m[1])
    .sort()
    .join(',');
const errors: string[] = sourceErrors(english);
const game = readFileSync(new URL('../../../../data/texts.list.txt', import.meta.url), 'utf8')
  .trim()
  .split(/\s+/)
  .filter((p) => !p.endsWith('keys.txt'))
  .map((p) => p.replace(/^data\/texts\.|\.txt$/g, ''))
  .sort();
if (JSON.stringify(game) !== JSON.stringify(locales.map((l) => l.gameCode).sort()))
  errors.push('Online languages do not match the game language inventory');
if (!keys.length) errors.push('English catalog is empty');
for (const locale of locales) {
  const catalog = read(locale.code);
  const missing = keys.filter((k) => !Object.hasOwn(catalog, k));
  const extra = Object.keys(catalog).filter((k) => !Object.hasOwn(english, k));
  if (missing.length || extra.length)
    errors.push(
      `${locale.code}: ${missing.length} missing, ${extra.length} extra keys: ${[...missing, ...extra].slice(0, 3).join(' | ')}`,
    );
  for (const family of pluralSources) {
    if (!Object.hasOwn(english, family.singular) || !Object.hasOwn(english, family.plural))
      errors.push(`Plural source missing: ${family.singular}`);
    if (typeof catalog[family.singular] !== 'object')
      errors.push(`${locale.code}: missing plural forms for ${family.singular}`);
  }
  for (const [source, message] of Object.entries(catalog)) {
    const variants = typeof message === 'string' ? { other: message } : message;
    if (typeof message !== 'string') {
      for (const category of new Intl.PluralRules(locale.code).resolvedOptions().pluralCategories) {
        if (!variants[category])
          errors.push(`${locale.code}: missing ${category} plural for ${source}`);
      }
    }
    for (const value of Object.values(variants)) {
      if (typeof value !== 'string' || !value.trim()) {
        errors.push(`${locale.code}: empty message ${source}`);
        continue;
      }
      if (locale.code !== 'en' && value === source && !sharedValues[locale.code]?.includes(source))
        errors.push(`${locale.code}: unreviewed English text: ${source}`);
      for (const token of invariants[source] ?? []) {
        if (!value.includes(token))
          errors.push(`${locale.code}: missing literal ${token} for ${source}`);
      }
      if (/98765[0-9]{3,}/.test(value))
        errors.push(`${locale.code}: translation marker leaked for ${source}`);
      if (placeholders(source) !== placeholders(value))
        errors.push(`${locale.code}: parameters differ for ${source}`);
      if (
        value.includes('\uFFFD') ||
        value.includes('\u0000') ||
        /\b(?:TODO|FIXME|TRANSLATE_ME)\b/.test(value)
      )
        errors.push(`${locale.code}: invalid translation ${source}`);
    }
  }
}
if (errors.length) {
  console.error(errors.slice(0, 50).join('\n'));
  console.error(`${errors.length} catalog errors`);
  process.exitCode = 1;
} else
  console.log(
    `${locales.length} languages × ${keys.length} messages: inventory, parameters and plural forms passed`,
  );
