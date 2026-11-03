import { createOpenAI } from '@ai-sdk/openai';
import { streamText, jsonSchema, tool } from 'ai';
import type { ProviderResult } from './store.ts';
export interface StudioProvider {
  generate(
    system: string,
    prompt: string,
    maxOutputTokens: number,
    signal: AbortSignal,
    progress: (text: string) => Promise<void>,
  ): Promise<ProviderResult>;
}
export interface CodingEdit {
  name: string;
  description: string;
  schema: Parameters<typeof jsonSchema>[0];
  encode: (input: unknown) => string;
}
export class CodingProvider implements StudioProvider {
  readonly key: string;
  readonly model: string;
  readonly edit?: CodingEdit;
  constructor(key: string, model: string, edit?: CodingEdit) {
    this.edit = edit;
    this.key = key;
    this.model = model;
  }
  async generate(
    system: string,
    prompt: string,
    maxOutputTokens: number,
    signal: AbortSignal,
    progress: (text: string) => Promise<void>,
  ): Promise<ProviderResult> {
    const result = streamText({
      model: createOpenAI({ apiKey: this.key }).responses(this.model),
      system,
      prompt,
      maxOutputTokens,
      maxRetries: 0,
      abortSignal: signal,
      tools: {
        [this.edit?.name ?? 'replace_source']: tool({
          description:
            this.edit?.description ??
            'Replace the current one-file JavaScript AI. Supply the complete source, not a diff. Only call when the user requested an edit.',
          inputSchema: jsonSchema(
            this.edit?.schema ?? {
              type: 'object',
              properties: { source: { type: 'string', minLength: 1, maxLength: 131072 } },
              required: ['source'],
              additionalProperties: false,
            },
          ),
        }),
      },
    });
    let text = '',
      last = 0;
    for await (const chunk of result.textStream) {
      text += chunk;
      if (Date.now() - last > 500) {
        await progress(text.slice(0, 16000));
        last = Date.now();
      }
    }
    const usage = await result.usage;
    if (usage.inputTokens === undefined || usage.outputTokens === undefined)
      throw Error('Provider usage missing; reconciliation required.');
    const calls = await result.toolCalls;
    const finish = await result.finishReason;
    const complete = finish === 'stop' || finish === 'tool-calls';
    const response = await result.response;
    // A malformed or truncated edit still has metered usage, but must never change the draft.
    const call = calls[0];
    const validEdit =
      calls.length === 1 &&
      call?.toolName === (this.edit?.name ?? 'replace_source') &&
      !('invalid' in call && call.invalid) &&
      (this.edit !== undefined || typeof (call.input as { source?: unknown })?.source === 'string');
    let source: string | undefined;
    let encodingError: string | undefined;
    if (complete && validEdit && call) {
      try {
        source = this.edit
          ? this.edit.encode(call.input)
          : (call.input as { source: string }).source;
      } catch {
        encodingError = 'The model returned an invalid replacement; the draft was left unchanged.';
      }
    }
    const editError =
      encodingError ??
      (!complete
        ? 'The model response did not complete; the source was left unchanged.'
        : calls.length && !validEdit
          ? 'The model returned an invalid or ambiguous edit; the source was left unchanged.'
          : undefined);
    return {
      text: text.slice(0, 16000),
      ...(editError ? { editError } : {}),
      ...(source === undefined ? {} : { source }),
      usage: {
        input: usage.inputTokens,
        cachedInput: usage.inputTokenDetails.cacheReadTokens ?? 0,
        output: usage.outputTokens,
      },
      responseId: response.id,
    };
  }
}
