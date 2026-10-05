/** A lost checkout response must retry the same purchase, including after reload.
 * Attempts are scoped by account and pack so changing packs cannot reuse an ID. */
export function checkoutAttempt(
  accountId: string,
  pack: string,
  storage: Pick<Storage, 'getItem' | 'setItem'> = sessionStorage,
): { id: string; pack: string } {
  const key = checkoutKey(accountId, pack);
  try {
    const saved = JSON.parse(storage.getItem(key) ?? 'null') as {
      id?: unknown;
      pack?: unknown;
    } | null;
    if (saved && typeof saved.id === 'string' && saved.pack === pack) return { id: saved.id, pack };
  } catch {
    /* Fall back to a new in-memory attempt if storage is unavailable. */
  }
  const attempt = { id: crypto.randomUUID(), pack };
  try {
    storage.setItem(key, JSON.stringify(attempt));
  } catch {
    /* Optional persistence. */
  }
  return attempt;
}
export function checkoutKey(accountId: string, pack: string) {
  return `ai-studio-checkout:${accountId}:${pack}`;
}
