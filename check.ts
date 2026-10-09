import {readFileSync} from 'node:fs';
import {defaultMapPool} from '../../platform/packages/core/src/queueConfig.ts';
import {parseCatalog,generateMapArgs} from '../../platform/packages/engine/src/engineCli.ts';
const catalog=parseCatalog(readFileSync('../artifacts/queue-revisions/catalog.json','utf8'));
for(const mode of ['1v1','2v2'] as const) {
 const pool=defaultMapPool(mode);
 for(const entry of pool) generateMapArgs({...entry,seed:20261001},catalog,'/tmp/glob2-pool-validation');
 console.log(mode+': '+pool.length+' descriptors accepted by engine argument validation');
}
