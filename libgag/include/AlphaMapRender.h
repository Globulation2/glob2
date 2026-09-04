#ifndef GAG_ALPHA_MAP_RENDER_H
#define GAG_ALPHA_MAP_RENDER_H

#include <vector>
#include <valarray>
#include <cstddef>
#include <algorithm>

// OpenGL headers must be included by the caller. Preserve the original four
// triangles and integer-rounded center alpha, but submit patches in bounded batches.
namespace GAGCore {
inline void drawAlphaMapBatched(const std::valarray<unsigned char>& map,
    int mapW, int mapH, int x, int y, int cellW, int cellH,
    unsigned char red, unsigned char green, unsigned char blue)
{
    if (mapW<2 || mapH<2) return;
    struct Vertex { float x,y; unsigned char r,g,b,a; };
    const int rowsPerBatch=std::min(mapH-1,std::max(1,4096/(mapW-1)));
    std::vector<Vertex> row((mapW-1)*12*rowsPerBatch);
    GLint oldBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2,GL_FLOAT,sizeof(Vertex),&row[0].x);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Vertex),&row[0].r);
    for (int dy=0;dy<mapH-1;++dy) {
        int top=y+dy*cellH, bottom=top+cellH;
        for (int dx=0;dx<mapW-1;++dx) {
            int left=x+dx*cellW, right=left+cellW;
            unsigned char tl=map[mapW*dy+dx], tr=map[mapW*dy+dx+1];
            unsigned char bl=map[mapW*(dy+1)+dx], br=map[mapW*(dy+1)+dx+1];
            unsigned char middle=((int(tl)+tr)/2+(int(bl)+br)/2)/2;
            Vertex center={float(left+cellW/2),float(top+cellH/2),red,green,blue,middle};
            Vertex corners[4]={
                {float(left),float(top),red,green,blue,tl},
                {float(left),float(bottom),red,green,blue,bl},
                {float(right),float(bottom),red,green,blue,br},
                {float(right),float(top),red,green,blue,tr}};
            for (int i=0;i<4;++i) {
                row[((dy%rowsPerBatch)*(mapW-1)+dx)*12+i*3]=center;
                row[((dy%rowsPerBatch)*(mapW-1)+dx)*12+i*3+1]=corners[i];
                row[((dy%rowsPerBatch)*(mapW-1)+dx)*12+i*3+2]=corners[(i+1)%4];
            }
        }
        if ((dy+1)%rowsPerBatch==0 || dy==mapH-2)
            glDrawArrays(GL_TRIANGLES,0,((dy%rowsPerBatch)+1)*(mapW-1)*12);
    }
    glPopClientAttrib();
    glBindBuffer(GL_ARRAY_BUFFER,oldBuffer);
}
}
#endif
