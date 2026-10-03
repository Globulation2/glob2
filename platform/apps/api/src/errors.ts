// Errors carrying a protocol ErrorBody. REST handlers throw them and the error
// handler sends the body; the realtime dispatcher turns them into failed
// responses with the same body.
import type { ErrorBody, ErrorCode } from '@glob2/protocol';

const STATUS: Record<ErrorCode, number> = {
  bad_request: 400,
  unauthenticated: 401,
  forbidden: 403,
  access_denied: 403,
  not_found: 404,
  conflict: 409,
  rate_limited: 429,
  update_required: 426,
  unsupported: 501,
  unavailable: 503,
  internal: 500,
};

export class HttpError extends Error {
  readonly statusCode: number;
  readonly body: ErrorBody;
  constructor(statusCode: number, body: ErrorBody) {
    super(body.message);
    this.statusCode = statusCode;
    this.body = body;
  }
}

export function apiError(code: ErrorCode, message: string, details?: unknown): HttpError {
  return new HttpError(
    STATUS[code],
    details === undefined ? { code, message } : { code, message, details },
  );
}
