import { AI_TAGS } from '@glob2/protocol';

export interface AiBrowseState {
  query: string;
  tags: string[];
  sort: string;
  pages: number;
  focus: string;
  cursor: string;
}
const sorts = ['likes', 'newest', 'updated', 'downloads'];
const paths = ['/ais', '/ais/favourites', '/ais/mine'];

export function readAiBrowse(search: URLSearchParams): AiBrowseState {
  const pages = Number(search.get('pages'));
  const sort = search.get('sort') ?? '';
  const focus = search.get('focus') ?? '';
  const cursor = search.get('cursor') ?? '';
  return {
    query: (search.get('q') ?? '').trim().slice(0, 128),
    tags: [...new Set((search.get('tags') ?? '').split(','))]
      .filter((tag) => AI_TAGS.some((allowed) => allowed === tag))
      .slice(0, 5),
    sort: sorts.includes(sort) ? sort : 'likes',
    pages: Number.isInteger(pages) && pages > 0 ? Math.min(pages, 20) : 1,
    focus: /^[\w-]{1,64}$/.test(focus) ? focus : '',
    cursor: /^[\w-]{1,512}$/.test(cursor) ? cursor : '',
  };
}

export function aiBrowseUrl(path: string, state: AiBrowseState): string {
  const search = new URLSearchParams();
  if (state.query) search.set('q', state.query);
  if (state.tags.length) search.set('tags', state.tags.join(','));
  if (state.sort !== 'likes') search.set('sort', state.sort);
  if (state.pages > 1) search.set('pages', String(state.pages));
  if (state.cursor) search.set('cursor', state.cursor);
  if (state.focus) search.set('focus', state.focus);
  return (paths.includes(path) ? path : '/ais') + (search.size ? '?' + search : '');
}

/** Return links only target a bounded, known catalogue route on this site. */
export function aiReturnUrl(value: string | null): string {
  if (!value || value.length > 2048) return '/ais';
  const [path = '', search = ''] = value.split('?');
  if (!paths.includes(path)) return '/ais';
  return aiBrowseUrl(path, readAiBrowse(new URLSearchParams(search)));
}
