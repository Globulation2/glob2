import { mkdtempSync, writeFileSync, rmSync, readFileSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, sep, dirname } from 'node:path';
import { describe, expect, it } from 'vitest';
import {
  sourceErrors,
  browserSourceErrors,
  fixedLabelErrors,
  fixedMetadataSourceErrors,
  fixedMetadataSources,
} from '../scripts/source-contract.ts';
import { AI_TAGS } from '../../protocol/src/ais.ts';
import english from '../locales/en.json' with { type: 'json' };

function check(source: string) {
  const root = mkdtempSync(join(tmpdir(), 'glob2-source-contract-'));
  try {
    writeFileSync(join(root, 'example.tsx'), source);
    return sourceErrors({ 'Sign in': 'Sign in', '/api/private': '/api/private' }, root + sep);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
}

describe('localized source contract', () => {
  it('requires English keys for fixed AI labels used through computed calls', () => {
    expect(fixedLabelErrors(english, AI_TAGS, 'AI tags')).toEqual([]);
    const incomplete: Record<string, unknown> = { ...english };
    delete incomplete.Rush;
    expect(fixedLabelErrors(incomplete, AI_TAGS, 'AI tags')).toEqual([
      'AI tags: missing catalog key: Rush',
    ]);
  });
  it('catches inaccessible labels and render-prop copy as well as visible text', () => {
    const errors = check(
      `const x = <button aria-label="Delete account">Delete{() => <span>{'Save'}</span>}</button>;`,
    );
    expect(errors).toHaveLength(3);
    expect(errors.every((error) => error.includes('unlocalized JSX text'))).toBe(true);
  });
  it('requires keys and keeps technical URLs out of translation calls', () => {
    const errors = check(`t('Sign in'); translate('Forgot password'); t('/api/private');`);
    expect(errors).toHaveLength(2);
    expect(errors[0]).toContain('missing catalog key');
    expect(errors[1]).toContain('technical URL translated');
    expect(
      check(`<RichMessage source="Many credits" singular={'One credit'} count={count} />`),
    ).toHaveLength(2);
  });
  it('catches conditional labels and templates inside rendered rich slots', () => {
    expect(
      check(`<button title={busy ? 'Saving' : 'Save'}>{paused ? 'Resume' : 'Pause'}</button>`),
    ).toHaveLength(4);
    expect(
      check('<RichMessage source="Sign in" slots={{ value: `${count} teams` }} />'),
    ).toHaveLength(1);
    expect(check('<span>{count && `${count} players`}</span>')).toHaveLength(1);
  });
  it('requires catalog keys for deferred user-facing error messages', () => {
    expect(check("throw new MessageError('Recovery warning');")).toHaveLength(1);
    expect(check("throw new MessageError('Sign in');")).toEqual([]);
    expect(check("throw new Error('Private diagnostic');")).toEqual([]);
  });
  it('allows localized labels, dynamic slots and technical editor syntax', () => {
    expect(
      check(
        `const x = <input placeholder={t('Sign in')} value={code} title={name} />; const y = <pre>{'terrain:natural,feature:lakes'}</pre>;`,
      ),
    ).toEqual([]);
  });
});

describe('browser launcher catalog contract', () => {
  it('checks HTML labels, escaped attributes and conditional message calls', () => {
    const html = `<!-- <div data-i18n="Comment only"></div> -->
<p data-i18n="Ready &amp; waiting"></p>
<button data-i18n-aria='Close'></button>
<script>
// browserText('Comment only');
const example = "browserText('Quoted example')";
const template = '<p data-i18n="Quoted attribute"></p>';
browserText(loaded ? 'Ready' : 'Loading');
browserText('Missing');
</script>`;
    const errors = browserSourceErrors(
      {
        'Ready & waiting': 'Ready & waiting',
        Close: 'Close',
        Ready: 'Ready',
        Loading: 'Loading',
      },
      html,
    );
    expect(errors).toHaveLength(1);
    expect(errors[0]).toContain('missing browser catalog key: Missing');
  });
  it('ignores scripts inside HTML comments while preserving active source locations', () => {
    const html = `<!--
<script>browserText('Hidden');</script>
-->
<script>browserText('Visible');</script>`;
    expect(browserSourceErrors({}, html)).toEqual([
      'platforms/web/shell.html:4: missing browser catalog key: Visible',
    ]);
  });
  it('retains calls between HTML-comment markers inside JavaScript strings', () => {
    const html = `<script>
const start = '<!--';
browserText('Visible');
const end = '-->';
</script>`;
    expect(browserSourceErrors({}, html)).toEqual([
      'platforms/web/shell.html:3: missing browser catalog key: Visible',
    ]);
  });
  it('detects a missing accessibility binding with a source location', () => {
    expect(browserSourceErrors({}, `<div>\n<input data-i18n-aria="Language"></div>`)).toEqual([
      'platforms/web/shell.html:2: missing browser catalog key: Language',
    ]);
  });
});

describe('fixed UI metadata catalog contract', () => {
  it('requires fixed display metadata even when JSX translates a variable', () => {
    const english = JSON.parse(
      readFileSync(new URL('../locales/en.json', import.meta.url), 'utf8'),
    ) as Record<string, unknown>;
    expect(fixedMetadataSourceErrors(english)).toEqual([]);
    delete english.Rush;
    const errors = fixedMetadataSourceErrors(english);
    expect(errors).toHaveLength(1);
    expect(errors[0]).toContain(': missing fixed UI catalog key: Rush');
  });
});

it('collects future metadata from declarations without requiring schema documentation', () => {
  const root = mkdtempSync(join(tmpdir(), 'glob2-fixed-metadata-'));
  try {
    for (const file of new Set(fixedMetadataSources().map((entry) => entry.file))) {
      const target = join(root, file);
      mkdirSync(dirname(target), { recursive: true });
      writeFileSync(target, readFileSync(new URL('../../../../' + file, import.meta.url)));
    }
    const path = join(root, 'platform/packages/protocol/src/ais.ts');
    writeFileSync(
      path,
      readFileSync(path, 'utf8').replace("'Balanced',", "'Balanced', 'Future display label',") +
        "\nconst schemaExample = Type.String({ description: 'Private schema documentation' });\n",
    );
    const sources = fixedMetadataSources(root).map((entry) => entry.source);
    expect(sources).toContain('Future display label');
    expect(sources).not.toContain('Private schema documentation');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
