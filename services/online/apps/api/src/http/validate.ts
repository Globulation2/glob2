import type { Static, TSchema } from 'typebox';
import { schemaIssues } from '@glob2/protocol';
import { apiError } from '../errors.ts';

/** The request body typed as the schema, or a bad_request error listing the problems. */
export function body<T extends TSchema>(schema: T, value: unknown): Static<T> {
  const issues = schemaIssues(schema, value ?? {});
  if (issues.length > 0) throw apiError('bad_request', 'Invalid request body.', issues);
  return (value ?? {}) as Static<T>;
}
