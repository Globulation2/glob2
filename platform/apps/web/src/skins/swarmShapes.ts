import { SWARM_MESHES, type SwarmMeshId } from '@glob2/protocol';

/** What players see for each swarm mesh, in catalog order. */
export const SWARM_SHAPES: Record<SwarmMeshId, { name: string; description: string }> = {
  classic: { name: 'Classic', description: 'The original swarm: spires ringed around an egg.' },
  crown: { name: 'Crown', description: 'Seven smooth spires around a central egg.' },
  clutch: { name: 'Clutch', description: 'Six brood eggs nested in a low rim.' },
  toadstool: { name: 'Toadstool', description: 'A broad-capped toadstool with young caps.' },
  coral: { name: 'Coral', description: 'Branching spires rising from a mound.' },
  skep: { name: 'Skep', description: 'A coiled hive on a stand, its door facing you.' },
  bloom: { name: 'Bloom', description: 'Petals cupped open around a central bud.' },
};

/** The designer's preview mesh, served from /skins/models/<model>.gsk. */
export function swarmModel(mesh: SwarmMeshId) {
  return mesh === 'classic' ? 'swarm' : `swarm-${mesh}`;
}

export function isSwarmMesh(value: unknown): value is SwarmMeshId {
  return SWARM_MESHES.some((mesh) => mesh === value);
}
