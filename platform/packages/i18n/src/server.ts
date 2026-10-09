import { readFileSync } from 'node:fs';
import { locales, registerCatalog, type Catalog } from './index.ts';
for (const locale of locales) {
  registerCatalog(
    locale.code,
    JSON.parse(
      readFileSync(new URL(`../locales/${locale.code}.json`, import.meta.url), 'utf8'),
    ) as Catalog,
  );
}
export * from './index.ts';
