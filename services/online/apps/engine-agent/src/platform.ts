// The engine agent's only connection to the platform: platform-api's internal
// engine API (protocol jobs.ts, "engine-agent HTTP API"), authenticated with a
// bearer agent key. No database or blob-store credentials live in this process.
import {
  ENGINE_LEASE_HEADER,
  EngineBlobReceipt,
  EngineLease,
  parse,
  type AiValidationReport,
  type EngineAgentHeartbeat,
  type EngineBlobReceipt as EngineBlobReceiptType,
  type EngineJobReport,
  type EngineLease as EngineLeaseType,
  type EngineLeaseRequest,
} from '@glob2/protocol';
import { BlobNotFoundError } from './blobs.ts';
import { EngineInputError } from './engineCli.ts';

/** A platform answer that is not a transport failure (4xx other than 408/429). */
export class PlatformRejection extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = 'PlatformRejection';
    this.status = status;
  }
}

export interface PlatformClientOptions {
  /** platform-api base URL on the backend network, e.g. http://platform-api:8080 */
  baseUrl: string;
  /** Bearer agent key. */
  key: string;
  /** Per-request timeout (default 30 s; blob transfers get 10 minutes). */
  timeoutMs?: number;
  fetch?: typeof fetch;
}

export class PlatformClient {
  private readonly base: string;
  private readonly key: string;
  private readonly timeoutMs: number;
  private readonly fetchImpl: typeof fetch;

  constructor(options: PlatformClientOptions) {
    this.base = options.baseUrl.replace(/\/+$/, '');
    this.key = options.key;
    this.timeoutMs = options.timeoutMs ?? 30_000;
    this.fetchImpl = options.fetch ?? fetch;
  }

  private async call(
    method: string,
    path: string,
    options: {
      json?: unknown;
      body?: Uint8Array;
      lease?: string;
      timeoutMs?: number;
      contentType?: string;
      /** Return a 404 answer instead of throwing. */
      allow404?: boolean;
    } = {},
  ): Promise<Response> {
    const headers: Record<string, string> = { authorization: `Bearer ${this.key}` };
    if (options.lease) headers[ENGINE_LEASE_HEADER] = options.lease;
    let body: RequestInit['body'];
    if (options.json !== undefined) {
      headers['content-type'] = 'application/json';
      body = JSON.stringify(options.json);
    } else if (options.body) {
      headers['content-type'] = options.contentType ?? 'application/octet-stream';
      body = options.body as unknown as RequestInit['body'];
    }
    const response = await this.fetchImpl(`${this.base}${path}`, {
      method,
      headers,
      ...(body === undefined ? {} : { body }),
      signal: AbortSignal.timeout(options.timeoutMs ?? this.timeoutMs),
    });
    if (response.ok || (options.allow404 && response.status === 404)) return response;
    const text = await response.text().catch(() => '');
    let message = `${method} ${path}: HTTP ${response.status}`;
    try {
      message += `: ${(JSON.parse(text) as { message?: string }).message ?? text.slice(0, 200)}`;
    } catch {
      message += text ? `: ${text.slice(0, 200)}` : '';
    }
    // Overload and timeouts are worth retrying; other refusals are not.
    if (response.status >= 500 || response.status === 408 || response.status === 429) {
      throw new Error(message);
    }
    throw new PlatformRejection(response.status, message);
  }

  async aiProgress(lease: string, report: AiValidationReport): Promise<void> {
    await this.call('POST', '/internal/v1/engine/ai-validation-progress', { lease, json: report });
  }

  async heartbeat(beat: EngineAgentHeartbeat): Promise<void> {
    await this.call('POST', '/internal/v1/engine/agents/heartbeat', { json: beat });
  }

  async deregister(agentId: string): Promise<void> {
    await this.call('DELETE', `/internal/v1/engine/agents/${encodeURIComponent(agentId)}`);
  }

  /** The next job for this agent, or undefined when there is none. */
  async lease(request: EngineLeaseRequest): Promise<EngineLeaseType | undefined> {
    const response = await this.call('POST', '/internal/v1/engine/jobs/lease', { json: request });
    if (response.status === 204) return undefined;
    return parse(EngineLease, await response.json(), 'engine lease') as EngineLeaseType;
  }

  async extend(jobId: string, lease: string, leaseSeconds: number): Promise<void> {
    await this.call('POST', `/internal/v1/engine/jobs/${jobId}/extend`, {
      json: { leaseSeconds },
      lease,
    });
  }

  async release(jobId: string, lease: string, retryAfterSeconds: number): Promise<void> {
    await this.call('POST', `/internal/v1/engine/jobs/${jobId}/release`, {
      json: { retryAfterSeconds },
      lease,
    });
  }

  async report(jobId: string, lease: string, report: EngineJobReport): Promise<void> {
    await this.call('POST', `/internal/v1/engine/jobs/${jobId}/result`, { json: report, lease });
  }

  /** A blob the leased job names, refused (EngineInputError) beyond maxBytes. */
  async readBlob(lease: string, sha256: string, maxBytes: number): Promise<Uint8Array> {
    const response = await this.call('GET', `/internal/v1/engine/blobs/${sha256}`, {
      lease,
      timeoutMs: 600_000,
      allow404: true,
    });
    if (response.status === 404) throw new BlobNotFoundError(`blob ${sha256} not found`);
    const declared = Number(response.headers.get('content-length') ?? 'NaN');
    if (Number.isFinite(declared) && declared > maxBytes) {
      await response.body?.cancel();
      throw new EngineInputError(`blob ${sha256} is ${declared} bytes; limit ${maxBytes}`);
    }
    const chunks: Uint8Array[] = [];
    let total = 0;
    for await (const chunk of response.body ?? []) {
      total += chunk.length;
      if (total > maxBytes) {
        throw new EngineInputError(`blob ${sha256} exceeds ${maxBytes} bytes`);
      }
      chunks.push(chunk);
    }
    return Buffer.concat(chunks);
  }

  async writeBlob(
    lease: string,
    bytes: Uint8Array,
    contentType: string,
    visibility: 'public' | 'private',
  ): Promise<string> {
    const query = new URLSearchParams({ contentType, visibility });
    const response = await this.call('PUT', `/internal/v1/engine/blobs?${query}`, {
      body: bytes,
      lease,
      timeoutMs: 600_000,
    });
    const receipt = parse(
      EngineBlobReceipt,
      await response.json(),
      'blob receipt',
    ) as EngineBlobReceiptType;
    return receipt.sha256;
  }
}
