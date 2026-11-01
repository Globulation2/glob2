// SPDX-License-Identifier: GPL-3.0-or-later
import {landscape} from './landscape.js';
import {setup as lesson0} from './lessons/introduction.js';
import {setup as lesson1} from './lessons/food-supply.js';
import {setup as lesson2} from './lessons/healing.js';
import {setup as lesson3} from './lessons/walking-training.js';
import {setup as lesson4} from './lessons/swimming.js';
import {setup as lesson5} from './lessons/schools.js';
import {setup as lesson6} from './lessons/upgrades.js';
import {setup as lesson7} from './lessons/warrior-training.js';
import {setup as lesson8} from './lessons/fortifications.js';
import {setup as lesson9} from './lessons/defensive-tactics.js';
export function validateRequest(c) {
    if(c.request.width!==256 || c.request.height!==256 || c.request.teams!==2 || c.request.workers!==8)
        return 'Training requires 256×256, two colonies and eight workers per colony';
}
export function generate(c) {
    c.stage('training landscape');landscape(c);
    c.stage('lesson setup');
    const setups=[lesson0,lesson1,lesson2,lesson3,lesson4,lesson5,lesson6,lesson7,lesson8,lesson9];
    if(!setups[c.request.options.lesson]) return 'Lesson setup is not available';
    setups[c.request.options.lesson](c);
}

export function validateWorld(c) {
    // Finished-world checks, including the largest teaching footprint and its
    // circulation margin. Initial placement independently checks every entity.
    for(const [x,y,w,h] of [[85,90,6,6],[88,96,5,5],[95,94,4,8]])
        for(let yy=y;yy<y+h;yy++) for(let xx=x;xx<x+w;xx++)
            if(!c.buildable(yy*256+xx)) return 'Training building or access space is obstructed';
    for(const name of ['inn','hospital','racetrack','barracks','school','defencetower'])
        if(!c.canPlaceBuilding(0,name,0,86,91)) return 'Teaching construction site is blocked: '+name;
    if(!c.canPlaceBuilding(0,'swimmingpool',0,83,84)) return 'Swimming Pool shore site is blocked';
    if(c.request.options.lesson===4 && !c.canPlaceBuilding(0,'school',0,89,97)) return 'Water-job School site is blocked';
    for(let team=0;team<2;team++)
        if(c.toolkit.Settlements.countBuildings(team,'swarm')!==1) return 'Missing training colony';
}
