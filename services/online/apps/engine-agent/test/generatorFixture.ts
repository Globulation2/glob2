export const REAL_GENERATOR_PACKAGE = Buffer.from(
  JSON.stringify({
    formatVersion: 1,
    manifest: {
      id: 'test:shared-generator',
      name: 'Shared landscape',
      revision: 1,
      apiVersion: 1,
      tags: ['terrain:natural'],
      controls: [
        {
          id: 'roughness',
          label: 'Roughness',
          group: 'terrain',
          minimum: 0,
          maximum: 2,
          step: 1,
          default: 1,
          searchRange: [0, 2],
        },
        {
          id: 'season',
          label: 'Season',
          group: 'resources',
          kind: 'choice',
          choices: ['Summer', 'Winter'],
          default: 0,
          searchValues: [0, 1],
        },
      ],
    },
    modules: {
      'generator.js': `export function validateRequest(c){if(c.request.width<128||c.request.teams>2)return 'This example supports at least 128 tiles and at most two colonies';}
export function generate(c){const terrain=c.mask(c.torus.size(),2);c.toolkit.Sketch.writeVertices(terrain);c.addTeams();if(!c.toolkit.Pipeline.settleColonies('shared',team=>terrain,team=>({x:24+team*48,y:24})))return 'Cannot place colonies';c.toolkit.Pipeline.secureStartingCrops(c.torus);}`,
    },
  }),
);
