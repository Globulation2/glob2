/** Same-origin photo URLs never expose linked email identifiers. */
export function avatarUrl(id: string, revision = 0): string {
  return `/api/v1/accounts/${id}/avatar?v=${revision}`;
}
