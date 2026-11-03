export function mergeStudioThread<
  Thread extends {
    id: string;
    cursor?: string;
    messages: { id: string; created_at: string }[];
    requests: { id: string; created_at: string }[];
    history?: { messagesBefore?: string; requestsBefore?: string };
  },
>(current: Thread | undefined, next: Thread): Thread {
  if (!current || current.id !== next.id) return next;
  function merge<T extends { id: string; created_at: string }>(previous: T[], latest: T[]) {
    return [...new Map([...previous, ...latest].map((v) => [v.id, v])).values()].sort(
      (a, b) => (a.created_at ?? '').localeCompare(b.created_at ?? '') || a.id.localeCompare(b.id),
    );
  }
  // Mutation responses, pagination and live snapshots may resolve out of order.
  // Add missing history from an older view, but never roll back committed state.
  const incomingIsOlder =
    /^\d+$/.test(current.cursor ?? '') &&
    /^\d+$/.test(next.cursor ?? '') &&
    BigInt(next.cursor ?? '0') < BigInt(current.cursor ?? '0');
  const older = incomingIsOlder ? next : current;
  const newer = incomingIsOlder ? current : next;
  return {
    ...newer,
    messages: merge(older.messages, newer.messages),
    requests: merge(older.requests, newer.requests),
    history: current.history ?? next.history,
  };
}
