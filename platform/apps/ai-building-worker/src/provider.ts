import { recordAttemptUsage } from '@glob2/billing';
import { BuildingPlan } from '@glob2/building-studio';
import { randomUUID } from 'node:crypto';
import { isDeepStrictEqual } from 'node:util';
import { sql } from 'kysely';
import { type BuildingAiStudio, emitState, type RequestRow } from '@glob2/building-studio';
export class ProviderUncertain extends Error {}
export class ProviderBudget extends Error {}
export class ProviderRejected extends Error {
  readonly usage: unknown;
  constructor(message: string, usage?: unknown) {
    super(message);
    this.usage = usage;
  }
}
async function rejectionDetails(response: Response): Promise<string> {
  const reader = response.body?.getReader();
  if (!reader) return '';
  const chunks: Uint8Array[] = [];
  let size = 0;
  try {
    for (;;) {
      const part = await reader.read();
      if (part.done) break;
      size += part.value.length;
      if (size > 16384) return '';
      chunks.push(part.value);
    }
    const error = JSON.parse(Buffer.concat(chunks).toString())?.error;
    if (typeof error?.message !== 'string') return '';
    return ': ' + error.message.replace(/\p{Cc}/gu, ' ').slice(0, 500);
  } catch {
    return '';
  } finally {
    await reader.cancel().catch(() => undefined);
  }
}
export class Attempts {
  readonly studio: BuildingAiStudio;
  readonly dailyBudget: number;
  constructor(studio: BuildingAiStudio, dailyBudget: number) {
    this.studio = studio;
    this.dailyBudget = dailyBudget;
  }
  async run<T>(
    row: RequestRow,
    stage: string,
    model: string,
    input: unknown,
    call: () => Promise<T>,
  ): Promise<T> {
    const previous = (
      await sql<{
        status: string;
        output: T;
        model: string;
        input: unknown;
      }>`SELECT status,output,model,input FROM building_studio_attempts WHERE request_id=${row.id} AND stage=${stage}`.execute(
        this.studio.db,
      )
    ).rows[0];
    if (previous) {
      // Changed inputs cannot establish the outcome of an already dispatched call.
      if (!['completed', 'failed'].includes(previous.status))
        throw new ProviderUncertain('A previous provider call needs reconciliation.');
      if (previous.model !== model || !isDeepStrictEqual(previous.input, input))
        throw new ProviderRejected(
          'The saved provider call has different inputs; start a new request.',
        );
      if (previous.status === 'completed') return previous.output;
      throw new ProviderRejected(
        'The saved provider call failed; it will not be dispatched again.',
      );
    }
    await this.studio.db.transaction().execute(async (db) => {
      await sql`SELECT pg_advisory_xact_lock(hashtextextended('ai-building-studio-provider-budget',0))`.execute(
        db,
      );
      const count =
        (
          await sql<{
            n: number;
          }>`SELECT calls::int AS n FROM building_studio_provider_usage WHERE day=(now() AT TIME ZONE 'UTC')::date`.execute(
            db,
          )
        ).rows[0]?.n ?? 0;
      if (count >= this.dailyBudget)
        throw new ProviderBudget('The studio is at its daily service limit.');
      const current = (
        await sql`SELECT id FROM building_studio_requests WHERE id=${row.id} AND lease=${row.lease} AND status NOT IN ('ready','failed','uncertain') FOR UPDATE`.execute(
          db,
        )
      ).rows;
      if (!current.length) throw new Error('BuildingAiStudio lease lost.');
      await sql`INSERT INTO building_studio_attempts(id,request_id,stage,model,status,input) VALUES(${randomUUID()},${row.id},${stage},${model},'dispatched',${JSON.stringify(input)}::jsonb)`.execute(
        db,
      );
      await sql`UPDATE building_studio_requests SET status='dispatched',error=NULL,checkpoints=checkpoints-'serviceLimit' WHERE id=${row.id}`.execute(
        db,
      );
      await emitState(db, row, 'dispatched');
    });
    let returned = false;
    try {
      const output = await call();
      returned = true;
      await recordAttemptUsage(
        this.studio.db,
        'buildings',
        row.id,
        stage,
        (output as { usage?: unknown }).usage,
      );
      await this.studio.db.transaction().execute(async (db) => {
        const current = (
          await sql`SELECT id FROM building_studio_requests WHERE id=${row.id} AND lease=${row.lease} AND status='dispatched' FOR UPDATE`.execute(
            db,
          )
        ).rows;
        if (!current.length)
          throw new ProviderUncertain('Provider result arrived after its lease was lost.');
        const saved =
          await sql`UPDATE building_studio_attempts SET status='completed',output=${JSON.stringify(output)}::jsonb WHERE request_id=${row.id} AND stage=${stage} AND status='dispatched' RETURNING id`.execute(
            db,
          );
        if (saved.rows.length !== 1)
          throw new ProviderUncertain('Provider journal could not be completed.');
        await sql`UPDATE building_studio_requests SET status='processing' WHERE id=${row.id} AND lease=${row.lease} AND status='dispatched'`.execute(
          db,
        );
        await emitState(db, row, 'processing');
      });
      row.status = 'processing';
      return output;
    } catch (error) {
      if (error instanceof ProviderRejected)
        await recordAttemptUsage(this.studio.db, 'buildings', row.id, stage, error.usage);
      // A lost COMMIT acknowledgement is not a lost provider result. Check the
      // durable journal before turning a successfully saved response uncertain.
      if (returned) {
        try {
          const saved = (
            await sql<{
              status: string;
              output: T;
            }>`SELECT status,output FROM building_studio_attempts WHERE request_id=${row.id} AND stage=${stage}`.execute(
              this.studio.db,
            )
          ).rows[0];
          if (saved?.status === 'completed') return saved.output;
        } catch {
          throw new ProviderUncertain('Provider outcome requires reconciliation.');
        }
      }
      const uncertain = returned || error instanceof ProviderUncertain;
      try {
        await this.studio.db.transaction().execute(async (db) => {
          const current =
            await sql`SELECT id FROM building_studio_requests WHERE id=${row.id} AND lease=${row.lease} AND status='dispatched' FOR UPDATE`.execute(
              db,
            );
          if (!current.rows.length)
            throw new ProviderUncertain('Provider result arrived after its lease was lost.');
          await sql`UPDATE building_studio_attempts SET status=${uncertain ? 'uncertain' : 'failed'} WHERE request_id=${row.id} AND stage=${stage} AND status='dispatched'`.execute(
            db,
          );
          if (!uncertain) {
            await sql`UPDATE building_studio_requests SET status='processing' WHERE id=${row.id}`.execute(
              db,
            );
            await emitState(db, row, 'processing');
          }
        });
        if (uncertain) {
          const current = await this.studio.request(row.id);
          if (current?.status !== 'uncertain')
            await this.studio.checkpoint(row, 'uncertain', { failedStage: stage });
        }
      } catch {
        // The error transaction may also commit before its acknowledgement is
        // lost. Preserve a known rejection instead of trapping its reservation.
        let saved: { status: string; output: T } | undefined;
        try {
          saved = (
            await sql<{
              status: string;
              output: T;
            }>`SELECT status,output FROM building_studio_attempts WHERE request_id=${row.id} AND stage=${stage}`.execute(
              this.studio.db,
            )
          ).rows[0];
        } catch {
          throw new ProviderUncertain('Provider outcome requires reconciliation.');
        }
        if (saved?.status === 'completed') return saved.output;
        if (saved?.status === 'failed')
          throw new ProviderRejected('The saved provider call failed.');
        throw new ProviderUncertain('Provider outcome requires reconciliation.');
      }
      if (uncertain && !(error instanceof ProviderUncertain))
        throw new ProviderUncertain('Provider result could not be saved; reconciliation required.');
      throw error;
    }
  }
}
export interface BuildingProvider {
  text(
    model: string,
    prompt: string,
    maxOutput: number,
    signal: AbortSignal,
  ): Promise<{ text: string; usage: unknown; responseId?: string }>;
  image(
    model: string,
    prompt: string,
    images: Uint8Array[],
    transparent: boolean,
    signal: AbortSignal,
  ): Promise<{ bytes: Uint8Array; usage: unknown; responseId?: string }>;
}
/** Dispatch is journalled before entry; ambiguous outcomes never retry automatically. */
export class OpenAIBuildings implements BuildingProvider {
  readonly key: string;
  constructor(key: string) {
    this.key = key;
    if (!key) throw Error('BUILDING_OPENAI_API_KEY is required.');
  }
  private async call(path: string, body: string | FormData, signal: AbortSignal) {
    if (signal.aborted) throw new ProviderRejected('Cancelled before dispatch.');
    let response: Response;
    try {
      response = await fetch('https://api.openai.com/v1/' + path, {
        method: 'POST',
        headers: {
          Authorization: 'Bearer ' + this.key,
          ...(typeof body === 'string' ? { 'Content-Type': 'application/json' } : {}),
        },
        body,
        signal: AbortSignal.any([signal, AbortSignal.timeout(240000)]),
      });
    } catch {
      throw new ProviderUncertain('Provider request interrupted; reconciliation required.');
    }
    if (!response.ok) {
      if (response.status >= 500 || response.status === 408) {
        await response.body?.cancel();
        throw new ProviderUncertain('Provider outcome unknown.');
      }
      throw new ProviderRejected(
        `Provider refused this request (HTTP ${response.status})${await rejectionDetails(response)}`,
      );
    }
    const reader = response.body?.getReader();
    if (!reader) throw new ProviderUncertain('Missing provider response.');
    const chunks: Uint8Array[] = [];
    let size = 0;
    try {
      for (;;) {
        const part = await reader.read();
        if (part.done) break;
        size += part.value.length;
        if (size > 48 * 1024 * 1024) {
          await reader.cancel();
          throw Error('Oversized response');
        }
        chunks.push(part.value);
      }
      return JSON.parse(Buffer.concat(chunks).toString()) as Record<string, unknown>;
    } catch {
      throw new ProviderUncertain('Provider response could not be recovered.');
    }
  }
  async text(model: string, prompt: string, maxOutput: number, signal: AbortSignal) {
    const output = await this.call(
      'responses',
      JSON.stringify({
        model,
        store: false,
        input: prompt,
        max_output_tokens: maxOutput,
        text: {
          format: {
            type: 'json_schema',
            name: 'building_plan',
            strict: true,
            schema: BuildingPlan,
          },
        },
      }),
      signal,
    );
    if (output['status'] !== 'completed')
      throw new ProviderRejected('Assistant response did not complete.', output['usage']);
    const messages = (
      output['output'] as {
        type: string;
        phase?: string;
        content?: { type: string; text?: string }[];
      }[]
    ).filter((v) => v.type === 'message' && v.phase !== 'commentary');
    const message = messages.findLast((v) => v.phase === 'final_answer') ?? messages.at(-1);
    const text = (message?.content ?? [])
      .filter((c) => c.type === 'output_text')
      .map((c) => c.text ?? '')
      .join('');
    if (!text || text.length > 256 * 1024)
      throw new ProviderRejected('No usable design.', output['usage']);
    return { text, usage: output['usage'], responseId: String(output['id'] ?? '') };
  }
  async image(
    model: string,
    prompt: string,
    images: Uint8Array[],
    transparent: boolean,
    signal: AbortSignal,
  ) {
    const settings = {
      model,
      prompt,
      size: '1024x1024',
      quality: 'medium',
      output_format: 'png',
      background: transparent ? 'transparent' : 'opaque',
      n: 1,
    };
    let body: string | FormData = JSON.stringify(settings);
    if (images.length) {
      const form = new FormData();
      for (const [key, value] of Object.entries(settings)) form.set(key, String(value));
      for (const [i, bytes] of images.entries())
        form.append(
          'image[]',
          new Blob([Uint8Array.from(bytes)], { type: 'image/png' }),
          `reference-${i}.png`,
        );
      body = form;
    }
    const output = await this.call(
      images.length ? 'images/edits' : 'images/generations',
      body,
      signal,
    );
    const encoded = (output['data'] as { b64_json?: string }[])?.[0]?.b64_json;
    if (!encoded) throw new ProviderRejected('Image provider returned no PNG.', output['usage']);
    const bytes = Buffer.from(encoded, 'base64');
    if (!bytes.length || bytes.length > 16 * 1024 * 1024)
      throw new ProviderRejected('Image exceeds the limit.', output['usage']);
    return { bytes, usage: output['usage'], responseId: String(output['id'] ?? '') };
  }
}
