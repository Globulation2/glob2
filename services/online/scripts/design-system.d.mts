export interface DesignSystemMetadata {
  repository: string;
  sha: string;
  installHash: string;
}
export function resolveDesignSystem(): string;
export function syncDesignSystem(): DesignSystemMetadata;
export function stageDesignAssets(
  publicDir: string,
  metadata: DesignSystemMetadata,
  brand?: boolean,
): void;
