import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const sharp = createRequire(new URL('../../platform/package.json', import.meta.url))('sharp');
import { decode, projectPose, DEFAULT_CAMERA } from '../../platform/apps/web/src/skins/geometry.ts';
import { buildFillChart, patternValue, DEFAULT_PATTERN } from '../../platform/apps/web/src/skins/projection.ts';
const root = path.resolve(import.meta.dirname, '../..');
const tile=80, rows=9, cols=16, rowHeight=104;
for(const file of fs.readdirSync(path.join(root,'data/skins/colony-v1')).filter(f=>f.endsWith('.gsk'))) {
 const name=file.slice(0,-4), bytes=fs.readFileSync(path.join(root,'data/skins/colony-v1',file));
 const mesh=decode(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength));
 const view=JSON.parse(fs.readFileSync(path.join(root,'data/skins/colony-v1',name+'.view.json')));
 const chart=buildFillChart(mesh,view,name.split('-')[0]);
 const output=Buffer.alloc(tile*cols*(rows*rowHeight+30)*4); for(let i=0;i<output.length;i+=4)output.set([38,33,45,255],i);
 let row=0; const captions=[];
 for(const kind of ['stripes','spots','speckles'])for(const scale of [32,64,96]) {
  const texture=Array.from({length:65536},(_,i)=>patternValue(chart.x[i],chart.y[i],{...DEFAULT_PATTERN,kind,scale,whole:true}));
  captions.push(`<text x="8" y="${row*rowHeight+46}">${kind} · ${scale} · directions 0–7, poses 0 / 15 (swarm: 0–337°)</text>`);
  for(let col=0;col<cols;col++){
   const pose=projectPose(mesh,view,mesh.frames===1?0:Math.floor(col/2)*32+(col%2?15:0),{...DEFAULT_CAMERA,game:true,angle:mesh.frames===1?Math.floor(col*360/cols):0},1);
   const zs=new Float64Array(tile*tile).fill(Infinity);
   for(let t=0;t<mesh.indices.length;t+=3){
    const ids=[mesh.indices[t],mesh.indices[t+1],mesh.indices[t+2]], p=ids.map(id=>[(pose[id*6]+1)*tile/2,(1-pose[id*6+1])*tile/2,pose[id*6+2]]);
    const [a,b,c]=p, d=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]); if(Math.abs(d)<1e-8)continue;
    for(let y=Math.max(0,Math.floor(Math.min(a[1],b[1],c[1])));y<=Math.min(tile-1,Math.ceil(Math.max(a[1],b[1],c[1])));y++)
    for(let x=Math.max(0,Math.floor(Math.min(a[0],b[0],c[0])));x<=Math.min(tile-1,Math.ceil(Math.max(a[0],b[0],c[0])));x++) {
     const u=((b[1]-c[1])*(x+.5-c[0])+(c[0]-b[0])*(y+.5-c[1]))/d,v=((c[1]-a[1])*(x+.5-c[0])+(a[0]-c[0])*(y+.5-c[1]))/d,w=1-u-v; if(Math.min(u,v,w)<0)continue;
     const weights=[u,v,w],z=u*a[2]+v*b[2]+w*c[2];if(z>=zs[y*tile+x])continue;zs[y*tile+x]=z;
     const uv=[0,1].map(k=>Math.min(255,Math.max(0,Math.floor(ids.reduce((s,id,j)=>s+weights[j]*mesh.uv[id*2+k],0)*256))));
     const color=texture[uv[1]*256+uv[0]]?[237,146,82]:[240,235,222];
     const light=.45+.55*Math.max(0,ids.reduce((s,id,j)=>s+weights[j]*(pose[id*6+4]*.6+pose[id*6+5]*.8),0));
     output.set([...color.map(c=>Math.round(c*light)),255],((row*rowHeight+54+y)*tile*cols+col*tile+x)*4);
    }
   }
  }
  row++;
 }
 const svg=`<svg width="${tile*cols}" height="${rows*rowHeight+30}"><g font-family="sans-serif" fill="#e9dfc8" font-size="12"><text x="8" y="20">${name} · curated fill compatibility contact sheet (flat inspection lighting)</text>${captions.join('')}</g></svg>`;
 await sharp(output,{raw:{width:tile*cols,height:rows*rowHeight+30,channels:4}}).composite([{input:Buffer.from(svg)}]).png().toFile(path.join(import.meta.dirname,'fills-'+name+'.png'));
 console.log(name);
}
