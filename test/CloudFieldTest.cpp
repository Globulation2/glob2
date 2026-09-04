#include "../src/CloudField.h"
#include <cassert>
#include <iostream>
int main() {
    for (int width : {2048,4096,8192}) for (int height : {2048,4096})
    for (int time : {0,250,10000}) {
        CloudField field(width,height,time,300,1300,3,3550,120);
        for(float magnification : {1.0f,1.5f,2.0f})
        for(int x : {0,16,640,width-16,width+32}) for(int y : {0,192,height-16}) {
            auto expected=field.opacity(x,y,magnification);
            assert(expected<=120);
            assert(field.opacity(x+width,y-height,magnification)==expected);
            // A full-world atlas and a translated 2D viewport must sample
            // the identical geographical position, irrespective of their size.
            for(int viewportX : {0,32,992}) for(int viewportY : {0,64,1024}) {
                int localX=x-viewportX,localY=y-viewportY;
                CloudField anotherView(width,height,time,300,1300,3,3550,120);
                assert(anotherView.opacity(viewportX+localX,viewportY+localY,magnification)==expected);
            }
        }
        // Repeated rendering cannot advance wind or shape: only elapsed time can.
        auto first=field.opacity(512,640,1.5f);
        for(int draw=0;draw<100;++draw) assert(field.opacity(512,640,1.5f)==first);
    }
    std::cout << "World cloud coordinates, wrapped seams, magnification, and draw-independent animation passed\n";
}
