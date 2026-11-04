import type {
  GeneratorDetail,
  GeneratorList,
  GeneratorInfo,
  GeneratorUpload,
  GeneratorSettings,
} from '@glob2/protocol';
import { request } from './api.ts';
const path = (id: string) => '/api/v1/generators/' + encodeURIComponent(id);
export const generatorApi = {
  list: (query: Record<string, string | number | undefined>, signal?: AbortSignal) =>
    request<GeneratorList>('GET', '/api/v1/generators', { query, ...(signal ? { signal } : {}) }),
  detail: (id: string, signal?: AbortSignal) =>
    request<GeneratorDetail>('GET', path(id), signal ? { signal } : {}),
  upload: (file: File, example: GeneratorSettings) =>
    request<GeneratorUpload>('POST', '/api/v1/generator-uploads', {
      body: file,
      query: { example: JSON.stringify(example) },
    }),
  check: (id: string, signal?: AbortSignal) =>
    request<GeneratorUpload>(
      'GET',
      '/api/v1/generator-uploads/' + encodeURIComponent(id),
      signal ? { signal } : {},
    ),
  publish: (body: object, id?: string) =>
    request<GeneratorInfo>('POST', id ? path(id) + '/versions' : '/api/v1/generators', { body }),
  update: (id: string, body: object) => request<GeneratorInfo>('PATCH', path(id), { body }),
  remove: (id: string) => request('DELETE', path(id)),
  social: (id: string, kind: 'like' | 'favourite', active: boolean) =>
    request(active ? 'PUT' : 'DELETE', path(id) + '/' + kind),
  report: (id: string, reason: string, details: string) =>
    request('POST', path(id) + '/reports', { body: { reason, details } }),
};
