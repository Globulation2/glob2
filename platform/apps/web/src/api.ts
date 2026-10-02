import { InstanceInfo, schemaIssues, type ErrorBody } from '@glob2/protocol';

export class ApiError extends Error {
  readonly status: number;
  readonly body: ErrorBody | undefined;
  constructor(status: number, body: ErrorBody | undefined) {
    super(body?.message ?? `HTTP ${status}`);
    this.status = status;
    this.body = body;
  }
}

/** Fetches the instance description, checking it against the protocol schema. */
export async function fetchInstance(signal?: AbortSignal): Promise<InstanceInfo> {
  const response = await fetch('/api/v1/instance', { signal: signal ?? null });
  const body: unknown = await response.json().catch(() => undefined);
  if (!response.ok) throw new ApiError(response.status, body as ErrorBody | undefined);
  const issues = schemaIssues(InstanceInfo, body);
  if (issues.length > 0) throw new Error(`unexpected instance response: ${issues[0]?.message}`);
  return body as InstanceInfo;
}
