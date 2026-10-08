/** Normalize provider metering without retaining prompts or output. */
export function meteredUsage(
  value: unknown,
): { input: number; cached: number; output: number } | undefined {
  if (!value || typeof value !== 'object') return undefined;
  const u = value as Record<string, unknown>,
    details = u['input_tokens_details'] as { cached_tokens?: unknown } | undefined;
  const input = u['input'] ?? u['input_tokens'],
    output = u['output'] ?? u['output_tokens'],
    cached = u['cachedInput'] ?? details?.cached_tokens ?? 0;
  if (
    ![input, output, cached].every(
      (n) => typeof n === 'number' && Number.isSafeInteger(n) && n >= 0,
    ) ||
    (cached as number) > (input as number)
  )
    return undefined;
  return { input: input as number, cached: cached as number, output: output as number };
}
