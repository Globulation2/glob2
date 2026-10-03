import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import type { Studio } from '@glob2/map-studio';
import { type RequestRow } from '@glob2/map-studio';
export class ProviderUncertain extends Error {}
export class ProviderBudget extends Error {}
export interface ImageInput {
  hash: string;
  bytes: Uint8Array;
}
export interface MapProvider {
  text(
    model: string,
    prompt: string,
    schema?: unknown,
  ): Promise<{ text: string; usage: unknown; responseId?: string }>;
  image(
    model: string,
    prompt: string,
    images: ImageInput[],
  ): Promise<{ bytes: Uint8Array; usage: unknown; responseId?: string }>;
}
export class OpenAIMaps implements MapProvider {
  readonly key: string;
  constructor(key: string) {
    if (!key) throw new Error('MAP_OPENAI_API_KEY is required');
    this.key = key;
  }
  private async call(path: string, body: string | FormData) {
    let response: Response;
    try {
      response = await fetch('https://api.openai.com/v1/' + path, {
        method: 'POST',
        headers: {
          Authorization: 'Bearer ' + this.key,
          ...(typeof body === 'string' ? { 'Content-Type': 'application/json' } : {}),
        },
        body,
        signal: AbortSignal.timeout(240000),
      });
    } catch {
      throw new ProviderUncertain('Provider request interrupted; reconciliation required.');
    }
    if (!response.ok) {
      await response.body?.cancel();
      if (response.status >= 500 || response.status === 408)
        throw new ProviderUncertain('Provider outcome is unknown.');
      throw new Error('The image provider refused the request.');
    }
    const reader = response.body?.getReader();
    if (!reader) throw new ProviderUncertain('Provider response missing.');
    const chunks: Uint8Array[] = [];
    let length = 0;
    try {
      for (;;) {
        const part = await reader.read();
        if (part.done) break;
        length += part.value.length;
        if (length > 48 * 1024 * 1024) {
          await reader.cancel();
          throw new ProviderUncertain('Provider response exceeded the bound.');
        }
        chunks.push(part.value);
      }
    } catch (error) {
      if (error instanceof ProviderUncertain) throw error;
      throw new ProviderUncertain('Provider response was interrupted.');
    }
    try {
      return JSON.parse(Buffer.concat(chunks).toString()) as Record<string, unknown>;
    } catch {
      throw new ProviderUncertain('Provider response could not be decoded.');
    }
  }
  async text(model: string, prompt: string, schema?: unknown) {
    const output = await this.call(
      'responses',
      JSON.stringify({
        model,
        store: false,
        input: prompt,
        max_output_tokens: 4000,
        ...(schema
          ? {
              text: { format: { type: 'json_schema', name: 'map_examples', strict: true, schema } },
            }
          : {}),
      }),
    );
    if (output['status'] !== 'completed') throw new Error('AI discussion did not complete.');
    const messages = output['output'] as { content?: { type: string; text?: string }[] }[];
    const text = messages
      .flatMap((m) => m.content ?? [])
      .filter((c) => c.type === 'output_text')
      .map((c) => c.text ?? '')
      .join('');
    if (!text.trim() || text.length > 16000)
      throw new Error('AI discussion returned no usable reply.');
    return { text, usage: output['usage'] ?? null, responseId: String(output['id'] ?? '') };
  }
  async image(model: string, prompt: string, images: ImageInput[]) {
    const form = new FormData();
    form.set('model', model);
    form.set('prompt', prompt);
    form.set('size', '2048x2048');
    form.set('quality', 'medium');
    form.set('output_format', 'png');
    form.set('n', '1');
    for (const input of images)
      form.append(
        'image[]',
        new Blob([Uint8Array.from(input.bytes)], { type: 'image/png' }),
        input.hash + '.png',
      );
    const output = await this.call('images/edits', form);
    const data = output['data'] as { b64_json?: string }[];
    if (!data?.[0]?.b64_json) throw new Error('Provider returned no map image.');
    const bytes = Buffer.from(data[0].b64_json, 'base64');
    if (!bytes.length || bytes.length > 32 * 1024 * 1024)
      throw new Error('Provider image is invalid.');
    return { bytes, usage: output['usage'] ?? null };
  }
}
export class Attempts {
  readonly studio: Studio;
  readonly dailyBudget: number;
  constructor(studio: Studio, dailyBudget: number) {
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
      }>`SELECT status,output FROM studio_attempts WHERE request_id=${row.id} AND stage=${stage}`.execute(
        this.studio.db,
      )
    ).rows[0];
    if (previous) {
      if (previous.status === 'completed') return previous.output;
      throw new ProviderUncertain('A previous provider call needs reconciliation.');
    }
    await this.studio.db.transaction().execute(async (db) => {
      await sql`SELECT pg_advisory_xact_lock(hashtextextended('studio-provider-budget',0))`.execute(
        db,
      );
      const count =
        (
          await sql<{
            n: number;
          }>`SELECT count(*)::int AS n FROM studio_attempts WHERE created_at>=date_trunc('day',now() AT TIME ZONE 'UTC') AT TIME ZONE 'UTC'`.execute(
            db,
          )
        ).rows[0]?.n ?? 0;
      if (count >= this.dailyBudget)
        throw new ProviderBudget('The studio is at its daily service limit.');
      const current = (
        await sql`SELECT id FROM studio_requests WHERE id=${row.id} AND lease=${row.lease} AND status NOT IN ('ready','failed','uncertain') FOR UPDATE`.execute(
          db,
        )
      ).rows;
      if (!current.length) throw new Error('Studio lease lost.');
      await sql`INSERT INTO studio_attempts(id,request_id,stage,model,status,input) VALUES(${randomUUID()},${row.id},${stage},${model},'dispatched',${JSON.stringify(input)}::jsonb)`.execute(
        db,
      );
      await sql`UPDATE studio_requests SET status='dispatched' WHERE id=${row.id}`.execute(db);
    });
    let returned = false;
    try {
      const output = await call();
      returned = true;
      await this.studio.db.transaction().execute(async (db) => {
        const current = (
          await sql`SELECT id FROM studio_requests WHERE id=${row.id} AND lease=${row.lease} AND status='dispatched' FOR UPDATE`.execute(
            db,
          )
        ).rows;
        if (!current.length)
          throw new ProviderUncertain('Provider result arrived after its lease was lost.');
        const saved =
          await sql`UPDATE studio_attempts SET status='completed',output=${JSON.stringify(output)}::jsonb WHERE request_id=${row.id} AND stage=${stage} AND status='dispatched' RETURNING id`.execute(
            db,
          );
        if (saved.rows.length !== 1)
          throw new ProviderUncertain('Provider journal could not be completed.');
        await sql`UPDATE studio_requests SET status='processing' WHERE id=${row.id} AND lease=${row.lease} AND status='dispatched'`.execute(
          db,
        );
      });
      row.status = 'processing';
      return output;
    } catch (error) {
      const uncertain = returned || error instanceof ProviderUncertain;
      try {
        await sql`UPDATE studio_attempts SET status=${uncertain ? 'uncertain' : 'failed'} WHERE request_id=${row.id} AND stage=${stage} AND status='dispatched'`.execute(
          this.studio.db,
        );
        if (uncertain) {
          const current = await this.studio.request(row.id);
          if (current?.status !== 'uncertain')
            await this.studio.checkpoint(row, 'uncertain', { failedStage: stage });
        }
      } catch {
        throw new ProviderUncertain('Provider outcome requires reconciliation.');
      }
      if (uncertain && !(error instanceof ProviderUncertain))
        throw new ProviderUncertain('Provider result could not be saved; reconciliation required.');
      throw error;
    }
  }
}
