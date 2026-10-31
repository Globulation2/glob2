// SPDX-License-Identifier: GPL-3.0-or-later
// The geography is independent of lesson selection. Keep the teaching corridor
// compact; the surrounding land is real room for expansion, not travel padding.
export const regions = {
    home: [86,91,4,4], lakeShore: [83,84,4,4], schoolSite: [89,97,2,2],
    landmark: [91,85,20,20], enemyOutpost: [107,91,10,12],
    enemyApproach: [103,64,16,64], defense: [93,94,8,8],
    towerSite: [96,96,2,2], homeValley: [68,80,26,36]
};
export function landscape(c) {
    const t=c.torus, vertices=c.mask(t.size(),0);
    // Overlapping uneven lobes give the coast bays and headlands without an
    // artificial enclosing rectangle. Named randomness never depends on lesson.
    const lobes=[[113,119,76,71],[164,143,61,65],[91,173,48,44],[159,79,45,35]];
    const coast=Array.from({length:17*17},()=>c.bounded('coast',17)-8);
    for(let y=0;y<256;y++) for(let x=0;x<256;x++) {
        let land=false;
        const ix=x>>4,iy=y>>4,fx=x&15,fy=y&15;
        const noise=(coast[iy*17+ix]*(16-fx)*(16-fy)+coast[iy*17+ix+1]*fx*(16-fy)+coast[(iy+1)*17+ix]*(16-fx)*fy+coast[(iy+1)*17+ix+1]*fx*fy)/256;
        for(const [cx,cy,rx,ry] of lobes)
            if((x-cx)*(x-cx)*ry*ry+(y-cy)*(y-cy)*rx*rx<rx*rx*ry*ry*(1+noise/80)) land=true;
        vertices.set(y*256+x,land?2:0);
    }
    // Inland lake, with a gently irregular shoreline. Algae is exclusively in
    // water: building a School requires workers to learn to swim first.
    for(let y=80;y<=91;y++) for(let x=92;x<=103;x++)
        if((x-97)*(x-97)+2*(y-85)*(y-85)<48) vertices.set(y*256+x,0);
    // A second lake narrows the direct outpost approach to a broad pass. The
    // longer southern route stays available if the player experiments with walls.
    for(let y=104;y<=128;y++) for(let x=93;x<=105;x++)
        if(4*(x-99)*(x-99)+(y-115)*(y-115)<165) vertices.set(y*256+x,0);
    c.toolkit.Sketch.layBeaches(vertices,t);
    c.toolkit.Sketch.writeVertices(vertices);
    for(const [name,rect] of Object.entries(regions)) c.defineArea(name,...rect);
    const patch=(key,x,y,w,h)=>{
        const type=c.resourceType(key);
        for(let yy=y;yy<y+h;yy++) for(let xx=x;xx<x+w;xx++)
            c.setResource(yy*256+xx,type,0);
    };
    // Compact renewable farms with clear harvesting fronts; nearby timber and
    // quarries leave both expansion and upgrade footprints open.
    patch('wheat',76,86,6,6); patch('wheat',73,107,16,5);
    patch('trees',71,88,5,9); patch('rocks',89,109,3,5);
    patch('rocks',100,93,2,1);
    patch('wheat',109,108,9,4); patch('algae',96,84,3,3);
    patch('trees',145,136,7,9);patch('rocks',153,152,6,5);
    patch('wheat',130,159,12,5);
    // Finite dry copses and broader water-side woodland frame the open valleys.
    for(const [cx,cy,rx,ry] of [[57,129,6,16],[133,67,11,5],[182,119,9,13],[107,191,15,6]])
        for(let y=cy-ry;y<=cy+ry;y++) for(let x=cx-rx;x<=cx+rx;x++)
            if((x-cx)*(x-cx)*ry*ry+(y-cy)*(y-cy)*rx*rx<rx*rx*ry*ry && c.buildable(y*256+x))
                c.setResource(y*256+x,c.resourceType('trees'),0);
    c.measure('training.landscape.tiles',65536);
}
