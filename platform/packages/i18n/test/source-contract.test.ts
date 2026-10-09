import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, sep } from 'node:path';
import { describe, expect, it } from 'vitest';
import { sourceErrors } from '../scripts/source-contract.ts';

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
