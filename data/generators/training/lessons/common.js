// SPDX-License-Identifier: GPL-3.0-or-later
export function colony(c,{intro=false,workerLevel=0,workerSkills={},injured=false}={}) {
    c.addTeams();
    c.placeBuilding(0,'swarm',0,79,96,{stocked:1,production0:1,production1:0,production2:0});
    if(intro) c.placeBuilding(0,'inn',0,85,96,{workers:2,stocked:1});
    else {
        c.placeBuilding(0,'inn',0,75,100,{workers:2,stocked:1});
        c.placeBuilding(0,'inn',0,84,100,{workers:2,stocked:1});
    }
    c.placeBuilding(1,'swarm',0,112,96,{production0:0,production1:0,production2:0});
    c.placeBuilding(1,'inn',0,108,116,{workers:2,stocked:1});
    for(let i=0;i<8;i++) {
        c.placeUnit(0,0,workerLevel,76+i,103,{...workerSkills,...(injured&&i===4?{healthPercent:25}:{})});
        c.placeUnit(1,0,0,108+i,114);
    }
    c.placeUnit(0,1,0,82,94);
    c.discover(0,68,79,54,40);
    c.discover(1,104,88,20,30);
}
export function encounter(c,count=1) {
    c.placeBuilding(1,'warflag',0,112,103,{workers:count,range:1});
    for(let i=0;i<count;i++) c.placeUnit(1,2,0,108,99+i*2);
}
