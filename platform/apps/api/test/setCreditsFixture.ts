import type { MapSetCredits } from '@glob2/protocol';

export const SET_CREDITS_FIXTURE: MapSetCredits = [
  {
    setId: '11111111-1111-4111-8111-111111111111',
    versionId: '22222222-2222-4222-8222-222222222222',
    title: 'Moss theme',
    license: 'CC-BY-4.0',
    sourceHash: 'ab'.repeat(32),
    entries: ['fixture:moss'],
    authors: [
      { author: 'Fixture artist', license: 'CC-BY-4.0', source: 'https://example.test/moss' },
    ],
  },
];
