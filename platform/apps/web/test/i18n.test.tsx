// @vitest-environment jsdom
import { afterEach, describe, expect, it } from 'vitest';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { useState } from 'react';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import { statusLabel, artifactLabel, fixedCaption } from '../src/messages.ts';
import { ApiError } from '../src/api.ts';
import {
  LocaleProvider,
  RichMessage,
  MessageError,
  translateError,
  t,
  useLocale,
} from '../src/i18n.tsx';

function Draft() {
  useLocale();
  const [draft, setDraft] = useState('');
  return (
    <>
      <h1>{t('Translation test heading')}</h1>
      <input aria-label="Draft" value={draft} onChange={(event) => setDraft(event.target.value)} />
      <RichMessage source="Read {link} first" slots={{ link: <a href="/guide">guide</a> }} />
    </>
  );
}
afterEach(async () => {
  cleanup();
  registerCatalog('en', {});
  await setLocale('en');
});
describe('web localization', () => {
  it('localizes known system artifact captions while preserving authored titles', async () => {
    registerCatalog('fr', {
      'Generated layout': 'Disposition générée',
      Reference: 'Référence',
      'Landscape reference {count}': 'Référence de paysage {count}',
      'Candidate {count} score': 'Partition du candidat {count}',
      'Colony {count}: starter food': 'Colonie {count} : nourriture initiale',
      'Engine import and artwork': 'Importation du moteur et graphismes',
    });
    await setLocale('fr');
    expect(artifactLabel({ label: 'Generated layout', kind: 'generated' })).toBe(
      'Disposition générée',
    );
    expect(artifactLabel({ label: 'Reference sheet 2', kind: 'reference' })).toBe('Référence 2');
    expect(artifactLabel({ label: 'My painted landscape', kind: 'preview' })).toBe(
      'My painted landscape',
    );
    expect(artifactLabel({ label: 'constructor' })).toBe('constructor');
    expect(artifactLabel({ label: 'Landscape reference 2', kind: 'reference' })).toBe(
      'Référence de paysage 2',
    );
    expect(artifactLabel({ label: 'Candidate 3 score', kind: 'source' })).toBe(
      'Partition du candidat 3',
    );
    expect(fixedCaption('Colony 4: starter food')).toBe('Colonie 4 : nourriture initiale');
    expect(fixedCaption('Engine import and artwork')).toBe('Importation du moteur et graphismes');
    expect(fixedCaption('Colony 4: my custom goal')).toBe('Colony 4: my custom goal');
  });
  it('updates an existing API error in the current locale without changing server metadata', async () => {
    registerCatalog('fr', { 'Too many requests.': 'Trop de requêtes.' });
    registerCatalog('it', { 'Too many requests.': 'Troppe richieste.' });
    await setLocale('fr');
    const body = { code: 'rate_limited' as const, message: 'Too many requests.' };
    const error = new ApiError(429, body);
    expect(error.message).toBe('Trop de requêtes.');
    expect(translateError(error)).toBe('Trop de requêtes.');
    await setLocale('it');
    expect(error.message).toBe('Troppe richieste.');
    expect(error.body).toBe(body);
    expect(body.message).toBe('Too many requests.');
    expect(error.status).toBe(429);
    expect(new ApiError(503, undefined).message).toBe('HTTP 503');
  });
  it('keeps unknown API diagnostics generic and isolates RTL error parameters once', async () => {
    registerCatalog('ar', {
      'Validation failed: {reason}': 'فشل التحقق: {reason}',
      'Something went wrong. Please try again.': 'حدث خطأ. حاول مرة أخرى.',
    });
    await setLocale('ar');
    const error = new ApiError(400, {
      code: 'bad_request',
      message: 'Validation failed: mapId',
      messageKey: 'Validation failed: {reason}',
      messageParams: { reason: 'mapId' },
    });
    expect(error.message).toBe('فشل التحقق: \u2068mapId\u2069');
    expect(translateError(error)).toBe(error.message);
    expect(new ApiError(500, { code: 'internal', message: 'private diagnostic' }).message).toBe(
      'حدث خطأ. حاول مرة أخرى.',
    );
  });
  it('renders missing inherited slot names as literal placeholders', () => {
    const { container } = render(<RichMessage source="{constructor} {toString}" slots={{}} />);
    expect(container.textContent).toBe('{constructor} {toString}');
  });
  it('preserves unknown enum labels that match Object prototype properties', () => {
    for (const value of ['constructor', 'toString', '__proto__']) {
      expect(statusLabel(value)).toBe(value);
    }
  });
  it('isolates LTR identifiers in localized RTL error metadata', async () => {
    registerCatalog('ar', { 'Validation failed: {reason}': 'فشل التحقق: {reason}' });
    await setLocale('ar');
    expect(
      translateError(new MessageError('Validation failed: {reason}', { reason: 'mapId' })),
    ).toBe('فشل التحقق: \u2068mapId\u2069');
  });
  it('retains error source parameters when the language changes', async () => {
    registerCatalog('fr', { 'Validation failed: {reason}': 'Échec de validation : {reason}' });
    registerCatalog('it', { 'Validation failed: {reason}': 'Validazione non riuscita: {reason}' });
    await setLocale('fr');
    const error = new MessageError('Validation failed: {reason}', { reason: 'detail' });
    expect(translateError(error)).toBe('Échec de validation : detail');
    await setLocale('it');
    expect(translateError(error)).toBe('Validazione non riuscita: detail');
  });

  it('updates text and document language while preserving an editor draft', async () => {
    registerCatalog('en', {});
    registerCatalog('fr', { 'Translation test heading': 'Titre du test' });
    await setLocale('en');
    render(
      <LocaleProvider>
        <Draft />
      </LocaleProvider>,
    );
    fireEvent.change(screen.getByLabelText('Draft'), { target: { value: 'unsaved draft' } });
    await act(() => setLocale('fr'));
    expect(screen.getByRole('heading').textContent).toBe('Titre du test');
    expect((screen.getByLabelText('Draft') as HTMLInputElement).value).toBe('unsaved draft');
    expect(document.documentElement.lang).toBe('fr');
  });
  it('lets translated sentences reorder React links without interpreting HTML', async () => {
    registerCatalog('fr', { 'Read {link} first': 'D’abord {link}, ensuite lire <script>' });
    await setLocale('fr');
    const { container } = render(
      <LocaleProvider>
        <Draft />
      </LocaleProvider>,
    );
    expect(container.textContent).toContain('D’abord guide, ensuite lire <script>');
    expect(screen.getByRole('link').getAttribute('href')).toBe('/guide');
    expect(container.querySelector('script')).toBeNull();
  });
});
