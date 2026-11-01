import { pendingGeneratorReport, type GeneratorVersion, type GeneratorInfo } from '@glob2/protocol';
export const id = '11111111-1111-4111-8111-111111111111',
  versionId = '22222222-2222-4222-8222-222222222222';
export const example = {
  seed: 19,
  params: { width: 7, height: 7, teams: 4, workers: 4 },
  candidates: 1,
  startingUnitLevel: 0 as const,
};
export const report = {
  ...pendingGeneratorReport('ab'.repeat(32), '133-54-' + 'cd'.repeat(32)),
  valid: true,
  fileHash: 'ab'.repeat(32),
  packageHash: 'ab'.repeat(32),
  metadata: {
    id: 'maple:river-country',
    name: 'River Country',
    description: 'Cross flowing water and grow sustainable colonies.',
    revision: 2,
    apiVersion: 1,
    toolkitVersion: 1,
    editorOnly: false,
    tags: ['terrain:natural', 'feature:rivers'],
    controls: [
      {
        id: 'water',
        label: 'Water',
        group: 'terrain',
        kind: 'range' as const,
        minimum: 0,
        maximum: 40,
        step: 5,
        default: 10,
      },
    ],
  },
  samples: [
    { settings: example, status: 'passed' as const, fingerprint: 'ab'.repeat(32) },
    {
      settings: { ...example, seed: 20 },
      status: 'refused' as const,
      message: 'This seed has no suitable colony sites.',
    },
  ],
};
export const version: GeneratorVersion = {
  id: versionId,
  hash: report.fileHash,
  packageHash: report.packageHash,
  label: '2.0',
  notes: 'More varied river crossings.',
  profile: 1,
  metadata: report.metadata,
  example,
  createdAt: '2026-10-04T12:00:00Z',
  downloads: 186,
  downloadUrl: `/api/v1/generators/${id}/versions/${versionId}/file`,
  validations: [report],
};
export const generator: GeneratorInfo = {
  id,
  name: 'River Country',
  description: report.metadata.description,
  tags: report.metadata.tags,
  visibility: 'public',
  hidden: false,
  owner: { id, displayName: 'Maple' },
  createdAt: version.createdAt,
  updatedAt: version.createdAt,
  likes: 42,
  downloads: 186,
  latestVersion: version,
  liked: false,
  favourited: false,
};
