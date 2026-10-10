#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
int main(int argc,char**argv){
 if(!SDL_Init(SDL_INIT_VIDEO)){puts(SDL_GetError());return 1;}
 auto*w=SDL_CreateWindow("Premultiply probe",256,256,SDL_WINDOW_HIDDEN);
 auto*r=SDL_CreateRenderer(w,argc>1?argv[1]:nullptr);if(!r){puts(SDL_GetError());return 2;}
 auto*s=SDL_CreateSurface(256,256,SDL_PIXELFORMAT_ARGB8888);
 for(int a=0;a<256;++a)for(int c=0;c<256;++c)((Uint32*)((char*)s->pixels+a*s->pitch))[c]=(a<<24)|(c<<16)|(c<<8)|c;
 auto*in=SDL_CreateTextureFromSurface(r,s);
 auto*out=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,256,256);
 if(!out){puts(SDL_GetError());return 3;}
 SDL_SetTextureScaleMode(in,SDL_SCALEMODE_NEAREST);SDL_SetTextureBlendMode(in,SDL_BLENDMODE_BLEND);
 SDL_SetRenderTarget(r,out);SDL_SetRenderDrawColor(r,0,0,0,0);SDL_RenderClear(r);SDL_RenderTexture(r,in,nullptr,nullptr);
 auto*read=SDL_RenderReadPixels(r,nullptr);if(!read){puts(SDL_GetError());return 4;}
 int mismatches=0,maxdelta=0;for(int a=0;a<256;++a)for(int c=0;c<256;++c){Uint8 rr,g,b,aa;Uint32 p;memcpy(&p,(char*)read->pixels+a*read->pitch+c*4,4);SDL_GetRGBA(p,SDL_GetPixelFormatDetails(read->format),nullptr,&rr,&g,&b,&aa);int wanted=(a*c+127)/255;int delta=abs(int(rr)-wanted);if(delta||aa!=a){if(mismatches<5)printf("a=%d c=%d actual=%d,%d expected=%d,%d\n",a,c,rr,aa,wanted,a);++mismatches;}if(delta>maxdelta)maxdelta=delta;}
 printf("renderer=%s mismatches=%d maxdelta=%d\n",SDL_GetRendererName(r),mismatches,maxdelta);
 SDL_DestroySurface(read);SDL_DestroySurface(s);SDL_SetRenderTarget(r,nullptr);SDL_DestroyTexture(in);SDL_DestroyTexture(out);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return mismatches?5:0;
}
