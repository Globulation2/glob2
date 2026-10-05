import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { type MusicStudio, emitState, type RequestRow } from '@glob2/music-studio';
export class ProviderUncertain extends Error {}
export class ProviderBudget extends Error {}
export class ProviderRejected extends Error {}
export class Attempts {
  readonly studio: MusicStudio;
  readonly dailyBudget: number;
  constructor(studio: MusicStudio, dailyBudget: number) {
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
      }>`SELECT status,output FROM music_studio_attempts WHERE request_id=${row.id} AND stage=${stage}`.execute(
        this.studio.db,
      )
    ).rows[0];
    if (previous) {
      if (previous.status === 'completed') return previous.output;
      throw new ProviderUncertain('A previous provider call needs reconciliation.');
    }
    await this.studio.db.transaction().execute(async (db) => {
      await sql`SELECT pg_advisory_xact_lock(hashtextextended('music-studio-provider-budget',0))`.execute(
        db,
      );
      const count =
        (
          await sql<{
            n: number;
          }>`SELECT calls::int AS n FROM music_studio_provider_usage WHERE day=(now() AT TIME ZONE 'UTC')::date`.execute(
            db,
          )
        ).rows[0]?.n ?? 0;
      if (count >= this.dailyBudget)
        throw new ProviderBudget('The studio is at its daily service limit.');
      const current = (
        await sql`SELECT id FROM music_studio_requests WHERE id=${row.id} AND lease=${row.lease} AND status NOT IN ('ready','failed','uncertain') FOR UPDATE`.execute(
          db,
        )
      ).rows;
      if (!current.length) throw new Error('MusicStudio lease lost.');
      await sql`INSERT INTO music_studio_attempts(id,request_id,stage,model,status,input) VALUES(${randomUUID()},${row.id},${stage},${model},'dispatched',${JSON.stringify(input)}::jsonb)`.execute(
        db,
      );
      await sql`UPDATE music_studio_requests SET status='dispatched',error=NULL,checkpoints=checkpoints-'serviceLimit' WHERE id=${row.id}`.execute(
        db,
      );
      await emitState(db, row, 'dispatched');
    });
    let returned = false;
    try {
      const output = await call();
      returned = true;
      await this.studio.db.transaction().execute(async (db) => {
        const current = (
          await sql`SELECT id FROM music_studio_requests WHERE id=${row.id} AND lease=${row.lease} AND status='dispatched' FOR UPDATE`.execute(
            db,
          )
        ).rows;
        if (!current.length)
          throw new ProviderUncertain('Provider result arrived after its lease was lost.');
        const saved =
          await sql`UPDATE music_studio_attempts SET status='completed',output=${JSON.stringify(output)}::jsonb WHERE request_id=${row.id} AND stage=${stage} AND status='dispatched' RETURNING id`.execute(
            db,
          );
        if (saved.rows.length !== 1)
          throw new ProviderUncertain('Provider journal could not be completed.');
        await sql`UPDATE music_studio_requests SET status='processing' WHERE id=${row.id} AND lease=${row.lease} AND status='dispatched'`.execute(
          db,
        );
        await emitState(db, row, 'processing');
      });
      row.status = 'processing';
      return output;
    } catch (error) {
      const uncertain = returned || error instanceof ProviderUncertain;
      try {
        await sql`UPDATE music_studio_attempts SET status=${uncertain ? 'uncertain' : 'failed'} WHERE request_id=${row.id} AND stage=${stage} AND status='dispatched'`.execute(
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
export interface ModelReply {
  text: string;
  usage: { input_tokens?: number; output_tokens?: number };
  responseId?: string;
}
export interface MusicProvider {
  text(model: string, prompt: string, maxOutput: number, signal: AbortSignal): Promise<ModelReply>;
}
/** Same Responses transport as Map Studio, with a per-request output allowance.
 * Requests are journalled by Attempts before this method is entered. */
export class OpenAIMusic implements MusicProvider {
  readonly key: string;
  constructor(key: string) {
    this.key = key;
    if (!key) throw new Error('MUSIC_OPENAI_API_KEY is required.');
  }
  async text(
    model: string,
    prompt: string,
    maxOutput: number,
    signal: AbortSignal,
  ): Promise<ModelReply> {
    let response: Response;
    try {
      response = await fetch('https://api.openai.com/v1/responses', {
        method: 'POST',
        headers: { Authorization: `Bearer ${this.key}`, 'Content-Type': 'application/json' },
        body: JSON.stringify({ model, store: false, input: prompt, max_output_tokens: maxOutput }),
        signal: AbortSignal.any([signal, AbortSignal.timeout(240000)]),
      });
    } catch {
      throw new ProviderUncertain('Provider request interrupted; reconciliation required.');
    }
    if (!response.ok) {
      await response.body?.cancel();
      if (response.status >= 500 || response.status === 408)
        throw new ProviderUncertain('Provider outcome is unknown.');
      throw new ProviderRejected('The music assistant could not accept this request.');
    }
    const reader = response.body?.getReader();
    if (!reader) throw new ProviderUncertain('Missing provider response.');
    const chunks: Uint8Array[] = [];
    let size = 0;
    try {
      for (;;) {
        const chunk = await reader.read();
        if (chunk.done) break;
        size += chunk.value.length;
        if (size > 2 * 1024 * 1024) {
          await reader.cancel();
          throw Error('Provider output too large');
        }
        chunks.push(chunk.value);
      }
      const output = JSON.parse(Buffer.concat(chunks).toString()) as {
        id: string;
        status: string;
        usage: ModelReply['usage'];
        output: { content?: { type: string; text?: string }[] }[];
      };
      if (output.status !== 'completed')
        throw new ProviderRejected('The assistant exhausted its output budget.');
      const text = output.output
        .flatMap((o) => o.content ?? [])
        .filter((c) => c.type === 'output_text')
        .map((c) => c.text ?? '')
        .join('');
      if (!text || text.length > 256 * 1024)
        throw new ProviderRejected('The assistant returned no usable composition.');
      return { text, usage: output.usage, responseId: output.id };
    } catch (error) {
      if (error instanceof ProviderRejected) throw error;
      throw new ProviderUncertain('Provider response could not be recovered.');
    }
  }
}
