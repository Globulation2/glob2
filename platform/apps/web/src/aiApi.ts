import type {
  AiList,
  AiDetail,
  AiInfo,
  AiUpload,
  AiSocialResult,
  PublishAiRequest,
  UpdateAiRequest,
} from '@glob2/protocol';
import { request } from './api.ts';
const path = (id: string) => '/api/v1/ais/' + encodeURIComponent(id);
export const aiApi = {
  list: (query: Record<string, string | number | undefined>, signal?: AbortSignal) =>
    request<AiList>('GET', '/api/v1/ais', { query, ...(signal ? { signal } : {}) }),
  detail: (id: string, signal?: AbortSignal) =>
    request<AiDetail>('GET', path(id), signal ? { signal } : {}),
  upload: (file: File, signal?: AbortSignal) =>
    request<AiUpload>('POST', '/api/v1/ai-uploads', { body: file, ...(signal ? { signal } : {}) }),
  check: (id: string, signal?: AbortSignal) =>
    request<AiUpload>(
      'GET',
      '/api/v1/ai-uploads/' + encodeURIComponent(id),
      signal ? { signal } : {},
    ),
  publish: (body: PublishAiRequest, id?: string) =>
    request<AiInfo>('POST', id ? path(id) + '/versions' : '/api/v1/ais', { body }),
  update: (id: string, body: UpdateAiRequest) => request<AiInfo>('PATCH', path(id), { body }),
  remove: (id: string) => request<void>('DELETE', path(id)),
  social: (id: string, kind: 'like' | 'favourite', active: boolean) =>
    request<AiSocialResult>(active ? 'PUT' : 'DELETE', path(id) + '/' + kind),
  report: (id: string, reason: string, details: string) =>
    request('POST', path(id) + '/reports', { body: { reason, details } }),
  hide: (id: string, hidden: boolean, reason: string) =>
    request(
      'POST',
      '/api/v1/admin/ais/' + encodeURIComponent(id) + (hidden ? '/hide' : '/unhide'),
      { body: { reason } },
    ),
};
