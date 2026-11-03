/** Project detail requests arrive newest first; conversation reading runs oldest first. */
export function projectConversation<T extends { status: string }>(requests: readonly T[]) {
  return {
    messages: requests.toReversed(),
    pending: requests.find((request) =>
      ['queued', 'running', 'uncertain'].includes(request.status),
    ),
    terminal: requests.find((request) =>
      ['completed', 'failed', 'cancelled'].includes(request.status),
    ),
  };
}
