// Platform REST client for the web app. The app is served from the instance
// origin, so requests carry the web session cookie (set by /signin); writes are
// same-origin fetches, which the API's CSRF check accepts.
import type {
  AdminAccount,
  AdminAccountList,
  AiLeaderboard,
  ErrorBody,
  InstanceInfo,
  InstanceStats,
  LeaderboardPage,
  MapDetail,
  MapInfo,
  MapLikeResult,
  MapList,
  MapReportList,
  MapReportReceipt,
  MapUpload,
  MapVersionInfo,
  MatchDetail,
  MatchList,
  PlayerProfile,
  SelfAccount,
} from '@glob2/protocol';

export class ApiError extends Error {
  readonly status: number;
  readonly body: ErrorBody | undefined;
  constructor(status: number, body: ErrorBody | undefined) {
    super(body?.message ?? `HTTP ${status}`);
    this.status = status;
    this.body = body;
  }
}

type Query = Record<string, string | number | undefined>;

function withQuery(path: string, query: Query = {}): string {
  const params = new URLSearchParams();
  for (const [key, value] of Object.entries(query)) {
    if (value !== undefined && value !== '') params.set(key, String(value));
  }
  const text = params.toString();
  return text ? `${path}?${text}` : path;
}

export async function request<T>(
  method: string,
  path: string,
  options: { query?: Query; body?: unknown; signal?: AbortSignal } = {},
): Promise<T> {
  const init: RequestInit = {
    method,
    credentials: 'same-origin',
    headers: { accept: 'application/json' },
    signal: options.signal ?? null,
  };
  if (options.body instanceof Blob || options.body instanceof ArrayBuffer) {
    init.headers = { ...init.headers, 'content-type': 'application/octet-stream' };
    init.body = options.body;
  } else if (options.body !== undefined) {
    init.headers = { ...init.headers, 'content-type': 'application/json' };
    init.body = JSON.stringify(options.body);
  }
  const response = await fetch(withQuery(path, options.query), init);
  if (response.status === 204) return undefined as T;
  const body: unknown = await response.json().catch(() => undefined);
  if (!response.ok) throw new ApiError(response.status, body as ErrorBody | undefined);
  return body as T;
}

const get = <T>(path: string, query?: Query, signal?: AbortSignal) =>
  request<T>('GET', path, { ...(query ? { query } : {}), ...(signal ? { signal } : {}) });

/**
 * Fetches the instance description, checking it against the protocol schema.
 * The schema library is large, so it loads alongside the request instead of
 * with the app's first script.
 */
export async function fetchInstance(signal?: AbortSignal): Promise<InstanceInfo> {
  const [body, protocol] = await Promise.all([
    get<unknown>('/api/v1/instance', undefined, signal),
    import('@glob2/protocol'),
  ]);
  const issues = protocol.schemaIssues(protocol.InstanceInfo, body);
  if (issues.length > 0) throw new Error(`unexpected instance response: ${issues[0]?.message}`);
  return body as InstanceInfo;
}

export const api = {
  stats: (signal?: AbortSignal) => get<InstanceStats>('/api/v1/stats', undefined, signal),
  me: (signal?: AbortSignal) => get<SelfAccount>('/api/v1/accounts/me', undefined, signal),
  signOut: () => request<undefined>('POST', '/api/v1/auth/web/sign-out', { body: {} }),

  leaderboard: (ladder: string, query: Query = {}, signal?: AbortSignal) =>
    get<LeaderboardPage>(`/api/v1/leaderboards/${encodeURIComponent(ladder)}`, query, signal),
  aiLeaderboard: (ladder: string, signal?: AbortSignal) =>
    get<AiLeaderboard>(`/api/v1/leaderboards/${encodeURIComponent(ladder)}/ai`, undefined, signal),
  player: (id: string, signal?: AbortSignal) =>
    get<PlayerProfile>(`/api/v1/players/${encodeURIComponent(id)}`, undefined, signal),
  playerMatches: (id: string, query: Query = {}, signal?: AbortSignal) =>
    get<MatchList>(`/api/v1/players/${encodeURIComponent(id)}/matches`, query, signal),
  matches: (query: Query = {}, signal?: AbortSignal) =>
    get<MatchList>('/api/v1/matches', query, signal),
  match: (id: string, signal?: AbortSignal) =>
    get<MatchDetail>(`/api/v1/matches/${encodeURIComponent(id)}`, undefined, signal),

  maps: (query: Query = {}, signal?: AbortSignal) => get<MapList>('/api/v1/maps', query, signal),
  map: (id: string, signal?: AbortSignal) =>
    get<MapDetail>(`/api/v1/maps/${encodeURIComponent(id)}`, undefined, signal),
  createMap: (body: {
    title: string;
    description?: string;
    visibility?: string;
    madeWith?: string;
  }) => request<MapInfo>('POST', '/api/v1/maps', { body }),
  updateMap: (id: string, body: { title?: string; description?: string; visibility?: string }) =>
    request<MapInfo>('PATCH', `/api/v1/maps/${encodeURIComponent(id)}`, { body }),
  deleteMap: (id: string) => request<undefined>('DELETE', `/api/v1/maps/${encodeURIComponent(id)}`),
  /** Checks a map file with the game before a catalog map is created for it. */
  checkMapFile: (file: Blob, fileName?: string) =>
    request<MapUpload>('POST', '/api/v1/uploads', {
      body: file,
      query: { format: 'map', fileName },
    }),
  checkedFile: (id: string, signal?: AbortSignal) =>
    get<MapUpload>(`/api/v1/uploads/${encodeURIComponent(id)}`, undefined, signal),
  uploadVersion: (id: string, file: Blob, notes?: string) =>
    request<MapVersionInfo>('POST', `/api/v1/maps/${encodeURIComponent(id)}/versions`, {
      body: file,
      ...(notes ? { query: { notes } } : {}),
    }),
  like: (id: string, liked: boolean) =>
    request<MapLikeResult>(liked ? 'PUT' : 'DELETE', `/api/v1/maps/${encodeURIComponent(id)}/like`),
  report: (id: string, reason: string, details: string) =>
    request<MapReportReceipt>('POST', `/api/v1/maps/${encodeURIComponent(id)}/reports`, {
      body: { reason, details },
    }),

  adminAccounts: (query: Query = {}, signal?: AbortSignal) =>
    get<AdminAccountList>('/api/v1/admin/accounts', query, signal),
  adminRename: (id: string, displayName: string, reason?: string) =>
    request<AdminAccount>('POST', `/api/v1/admin/accounts/${id}/rename`, {
      body: { displayName, ...(reason ? { reason } : {}) },
    }),
  adminMute: (id: string, minutes: number, reason?: string) =>
    request<AdminAccount>('POST', `/api/v1/admin/accounts/${id}/mute`, {
      body: { minutes, ...(reason ? { reason } : {}) },
    }),
  adminBan: (id: string, banned: boolean, reason?: string) =>
    request<AdminAccount>('POST', `/api/v1/admin/accounts/${id}/ban`, {
      body: { banned, ...(reason ? { reason } : {}) },
    }),
  adminMatches: (query: Query = {}, signal?: AbortSignal) =>
    get<MatchList>('/api/v1/admin/matches', query, signal),
  adminReports: (query: Query = {}, signal?: AbortSignal) =>
    get<MapReportList>('/api/v1/admin/map-reports', query, signal),
  resolveReport: (
    id: string,
    body: {
      status: 'resolved' | 'dismissed';
      note?: string;
      hideMap?: boolean;
      hideReason?: string;
    },
  ) => request<unknown>('POST', `/api/v1/admin/map-reports/${id}/resolve`, { body }),
  hideMap: (id: string, reason: string) =>
    request<unknown>('POST', `/api/v1/admin/maps/${id}/hide`, { body: { reason } }),
  unhideMap: (id: string) => request<unknown>('POST', `/api/v1/admin/maps/${id}/unhide`, {}),
};
