#include "TorusView.h"
#include "TorusGeometry.h"
#include "Game.h"
#include "Team.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <cstddef>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#else
#include <epoxy/gl.h>
#endif
#endif

namespace {
const float pi = 3.14159265358979323846f;
float clamp(float x, float a, float b) { return std::max(a, std::min(b, x)); }
float smooth(float x) { x = clamp(x, 0, 1); return x*x*(3-2*x); }
float mix(float a, float b, float t) { return a+(b-a)*t; }
#ifdef HAVE_OPENGL
struct SkyPoint { float x, y, z, brightness; int size; };
// Deterministic visual-only randomness, independent of simulation state.
const std::vector<SkyPoint> &skyPoints(bool haze) {
    static std::vector<SkyPoint> stars, clouds;
    auto &points = haze ? clouds : stars;
    if (!points.empty()) return points;
    unsigned seed = haze ? 81991u : 1729u;
    auto random = [&]() { seed = seed*1664525u+1013904223u; return (seed>>8)/16777216.0f; };
    for (int i=0; i<(haze ? 1700 : 5200); ++i) {
        float longitude = random()*2*pi;
        float latitude = haze ? (random()+random()+random()-1.5f)*0.10f : random()*2-1;
        float radius = std::sqrt(std::max(0.0f,1-latitude*latitude));
        float x = radius*std::cos(longitude), y = latitude, z = radius*std::sin(longitude);
        points.push_back({x*0.82f-y*0.572f,x*0.572f+y*0.82f,z,
            random(), i%29 == 0 ? 2 : (i%5 == 0 ? 1 : 0)});
    }
    return points;
}
void drawSky(float yaw, float pitch, float fade, int width, int height) {
    glUseProgram(0);
    glDisable(GL_TEXTURE_2D); glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glEnable(GL_POINT_SMOOTH);
    auto project = [&](const SkyPoint &p, float &x, float &y) {
        float xx=p.x*std::cos(yaw)+p.z*std::sin(yaw);
        float zz=-p.x*std::sin(yaw)+p.z*std::cos(yaw);
        float yy=p.y*std::cos(pitch)-zz*std::sin(pitch);
        zz=p.y*std::sin(pitch)+zz*std::cos(pitch);
        if (zz<0.15f) return false;
        x=width*0.5f+xx/zz*height*0.65f;
        y=height*0.5f+yy/zz*height*0.65f;
        return x>-80 && x<width+80 && y>-80 && y<height+80;
    };
    glPointSize(48);
    glBegin(GL_POINTS);
    for (const auto &p : skyPoints(true)) {
        float x,y;
        if (project(p,x,y)) {
            glColor4f(0.28f,0.33f,0.48f,fade*(0.003f+p.brightness*0.008f));
            glVertex2f(x,y);
        }
    }
    glEnd();
    for (int size=0;size<3;++size) {
        glPointSize(size==0 ? 1 : (size==1 ? 1.7f : 2.6f));
        glBegin(GL_POINTS);
        for (const auto &p : skyPoints(false)) {
            float x,y;
            if (p.size==size && project(p,x,y)) {
                float warm=p.brightness;
                glColor4f(mix(0.65f,1.0f,warm),mix(0.78f,0.90f,warm),mix(1.0f,0.72f,warm),
                    fade*(0.20f+p.brightness*0.65f));
                glVertex2f(x,y);
            }
        }
        glEnd();
    }
    glDisable(GL_POINT_SMOOTH);
    glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glEnable(GL_TEXTURE_2D);
}
GLuint createMaterial() {
    const char *vertex =
        "#version 120\n"
        "varying vec2 uv; varying vec3 light;\n"
        "uniform vec2 mapOffset;\n"
        "void main(){gl_Position=ftransform();uv=gl_MultiTexCoord0.xy+mapOffset;light=gl_Color.rgb;}\n";
    const char *fragment =
        "#version 120\n"
        "uniform sampler2D world; uniform sampler2D visibility; uniform float fold;\n"
        "varying vec2 uv; varying vec3 light;\n"
        "void main(){\n"
        " vec3 terrain=texture2D(world,uv).rgb;\n"
        " float seen=texture2D(visibility,uv).r;\n"
        " vec2 grid=abs(fract(uv*vec2(48.0,24.0)-0.5)-0.5);\n"
        " float line=1.0-smoothstep(0.0,0.035,min(grid.x,grid.y));\n"
        " vec3 unknown=vec3(0.22,0.31,0.41)+line*vec3(0.035,0.045,0.055);\n"
        " vec3 surface=mix(unknown*fold,terrain,seen);\n"
        " gl_FragColor=vec4(surface*light,1.0);\n"
        "}\n";
    GLuint vs=glCreateShader(GL_VERTEX_SHADER), fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex,0); glCompileShader(vs);
    glShaderSource(fs,1,&fragment,0); glCompileShader(fs);
    GLuint program=glCreateProgram();
    glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
    GLint okay=0; glGetProgramiv(program,GL_LINK_STATUS,&okay);
    if (!okay) {
        char log[2048]; glGetProgramInfoLog(program,sizeof(log),0,log);
        fprintf(stderr,"Torus material: %s\n",log);
        glDeleteProgram(program); program=0;
    }
    glDeleteShader(vs); glDeleteShader(fs);
    return program;
}
#endif
}

TorusView::TorusView() : target(false), dragging(false), amount(0), zoom(1), travelU(0), travelV(0),
    baseViewportX(0), baseViewportY(0), worldW(0), worldH(0), atlasW(0), atlasH(0),
    lastFrame(0), lastCapture(0), texture(0), visibility(0), framebuffer(0), material(0), meshBuffer(0), indexBuffer(0), meshKey{}, failed(false), originX(0), originY(0), focusU(0.5f), focusV(0.5f) {}
TorusView::~TorusView()
{
#ifdef HAVE_OPENGL
    if (SDL_GL_GetCurrentContext()) {
        if (meshBuffer) glDeleteBuffers(1,&meshBuffer);
        if (indexBuffer) glDeleteBuffers(1,&indexBuffer);
        if (material) glDeleteProgram(material);
        if (texture) glDeleteTextures(1, &texture);
        if (visibility) glDeleteTextures(1, &visibility);
        if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    }
#endif
}
bool TorusView::available() const
{
#ifdef HAVE_OPENGL
    return !failed && globalContainer->gfx && (globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU);
#else
    return false;
#endif
}
void TorusView::toggle() {
    if (available()) {
        if (!active()) resetCamera();
        target = !target; dragging = false; lastFrame = SDL_GetTicks();
    }
}
void TorusView::resetCamera() { zoom = 1; }
void TorusView::setViewport(int x, int y) {
    if (!worldW || !worldH || !active()) return;
    auto current=TorusGeometry::destination(baseViewportX,baseViewportY,travelU,travelV,worldW,worldH);
    travelU += float(TorusGeometry::wrappedDelta(current.x,x,worldW))/worldW;
    travelV += float(TorusGeometry::wrappedDelta(current.y,y,worldH))/worldH;
    travelU -= std::floor(travelU); travelV -= std::floor(travelV);
}
bool TorusView::event(const SDL_Event &e, int width, int &vx, int &vy)
{
    if (!active()) return false;
    if (e.type == SDL_MOUSEBUTTONUP && dragging) { dragging = false; return true; }
    if (e.type == SDL_MOUSEMOTION) {
        if (!(e.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK))) dragging = false;
        if (dragging && target && worldW && worldH) {
            // Move the camera focus across the fixed world surface.
            float pixelsPerWorld = std::max(1.0f,width*0.85f*zoom);
            travelU -= e.motion.xrel/pixelsPerWorld;
            travelV -= e.motion.yrel/pixelsPerWorld*float(worldW)/worldH;
            travelU -= std::floor(travelU); travelV -= std::floor(travelV);
            auto position=TorusGeometry::destination(baseViewportX,baseViewportY,travelU,travelV,worldW,worldH);
            vx=position.x; vy=position.y;
        }
        return dragging || e.motion.x < width;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.x < width) {
        if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_MIDDLE) dragging = true;
        if (e.button.button == SDL_BUTTON_RIGHT) resetCamera();
        return true;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.x < width) return true;
    if (e.type == SDL_MOUSEWHEEL) {
        int x, y; SDL_GetMouseState(&x, &y);
        if (x < width) {
            int direction=e.wheel.y*(e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
            if (target) {
                zoom=clamp(zoom*std::pow(1.12f,float(direction)),0.4f,2.0f);
                if (direction>0 && zoom>=2.0f) toggle();
            }
            return true;
        }
    }
    return false;
}

void TorusView::draw(Game &game, int team, unsigned options, int &vx, int &vy, int width, int height)
{
#ifdef HAVE_OPENGL
    Uint32 now = SDL_GetTicks();
    float dt = std::min(0.1f, float(now-lastFrame)/1000.0f);
    lastFrame = now;
    if (!amount && target) {
        // Put the current viewport center on the front of the torus. Preserve
        // its sub-tile offset, so even the first/last frame matches normal 2D.
        TorusGeometry::MapFocus focus = TorusGeometry::mapFocus(
            game.map.getW(), game.map.getH(), vx, vy, width, height);
        if (!worldW || !worldH) { originX=focus.originX; originY=focus.originY; }
        focusU=(((vx-originX)&game.map.getMaskW())+width/64.0f)/game.map.getW();
        focusV=(((vy-originY)&game.map.getMaskH())+(height+16)/64.0f)/game.map.getH();
        baseViewportX=vx; baseViewportY=vy;
        worldW=game.map.getW(); worldH=game.map.getH();
        travelU=travelV=0;
        lastCapture = 0;
    }
    amount = clamp(amount + (target ? dt : -dt)/1.8f, 0, 1);
    // The ordinary map and minimap track the same destination as the torus.
    // Ease the sub-tile remainder away while returning to the tile-based 2D camera.
    if (!target) {
        float settle=amount==0 ? 1 : 1-std::exp(-16*dt);
        travelU=mix(travelU,std::round(travelU*worldW)/worldW,settle);
        travelV=mix(travelV,std::round(travelV*worldH)/worldH,settle);
    }
    auto destination=TorusGeometry::destination(baseViewportX,baseViewportY,travelU,travelV,worldW,worldH);
    vx=destination.x; vy=destination.y;
    auto gfx = globalContainer->gfx;
    gfx->setClipRect();
    GLint oldViewport[4], oldMatrixMode, oldProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetIntegerv(GL_MATRIX_MODE, &oldMatrixMode);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    // Render the actual world (terrain, resources, buildings, units and fog) to
    // a bounded offscreen texture. Camera frames are independent of capture rate.
    GLint maximumTexture=0, maximumViewport[2]={0,0};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximumTexture);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS,maximumViewport);
    int nextW=std::min(worldW*32,std::min(8192,std::min(maximumTexture,maximumViewport[0])));
    int nextH=std::min(worldH*32,std::min(8192,std::min(maximumTexture,maximumViewport[1])));
    if (!texture || atlasW!=nextW || atlasH!=nextH) {
        if (texture) glDeleteTextures(1,&texture);
        if (framebuffer) glDeleteFramebuffers(1,&framebuffer);
        atlasW=nextW; atlasH=nextH;
        lastCapture=0;
        glPushAttrib(GL_TEXTURE_BIT);
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            target = false; amount = 0; failed = true;
            fprintf(stderr, "Torus view: offscreen framebuffer is unavailable\n");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPopAttrib();
        if (failed) {
            glMatrixMode(GL_MODELVIEW); glPopMatrix();
            glMatrixMode(GL_PROJECTION); glPopMatrix();
            glMatrixMode(oldMatrixMode);
            return;
        }
        if (!material) material = createMaterial();
    }
    if (!lastCapture || now-lastCapture >= 100) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, atlasW, atlasH);
        glOrtho(0, game.map.getW()*32, game.map.getH()*32, 0, -1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // Capture the normal cloud and shadow layers along with the world,
        // respecting the same graphics-quality setting as the 2D view.
        game.drawMap(0, 0, game.map.getW()*32, game.map.getH()*32, 0, 0, originX, originY, team, options);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // Discovery comes from the map, independently of cloud brightness.
        // The flipped rows and half-tile sample centers match the world atlas.
        std::vector<unsigned char> discovered(worldW*worldH*4);
        Uint32 visibleTeams=globalContainer->replaying ? globalContainer->replayVisibleTeams : game.teams[team]->me;
        for (int y=0;y<worldH;++y) for (int x=0;x<worldW;++x) {
            unsigned char value=(options & Game::DRAW_WHOLE_MAP) ||
                game.map.isMapDiscovered(originX+x,originY+y,visibleTeams) ? 255 : 0;
            for (int c=0;c<4;++c) discovered[((worldH-1-y)*worldW+x)*4+c]=value;
        }
        glPushAttrib(GL_TEXTURE_BIT);
        if (!visibility) glGenTextures(1,&visibility);
        glBindTexture(GL_TEXTURE_2D,visibility);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,worldW,worldH,0,GL_RGBA,GL_UNSIGNED_BYTE,discovered.data());
        glPopAttrib();
        lastCapture = now;
    }

    // Save GL state AFTER the game renderer: its state cache must still match
    // the restored state when the ordinary HUD resumes drawing.
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, width, height-16);
    glClearColor(0.025f, 0.037f, 0.06f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_RECTANGLE_ARB); glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, gfx->getW(), gfx->getH(), 0, -10000, 10000);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    float pull = smooth(amount);
    float roll = smooth(amount);
    float aspect = float(game.map.getW())/game.map.getH();
    // One direct, restrained pullback. There is no intermediate zoom to a
    // distant full-map sheet, then zoom back in to the torus.
    float anchorU=focusU+travelU, anchorV=focusV+travelV;
    float cameraDistance=TorusGeometry::hoverDistance(anchorV);
    float scale = std::min(width/10.0f, height/9.0f) * mix(1, zoom, roll)
        * TorusGeometry::hoverScale(anchorV);
    float sx = std::exp(mix(std::log(game.map.getW()*32/(8*pi)), std::log(scale), pull));
    float sy = sx*TorusGeometry::verticalScale(focusU, focusV, roll, aspect);
    float cx = width*0.5f, cy = (height+16)*0.5f;
    float major = smooth(roll), minor = smooth(roll/0.85f);
    float ya=-(anchorU-0.5f)*2*pi, pa=TorusGeometry::latitude(anchorV,aspect);
    drawSky(ya, pa, pull, width, height);
    if (material) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,visibility);
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(material);
        glUniform1i(glGetUniformLocation(material, "world"), 0);
        glUniform1i(glGetUniformLocation(material, "visibility"), 1);
        glUniform1f(glGetUniformLocation(material, "fold"), pull);
        glUniform2f(glGetUniformLocation(material, "mapOffset"),anchorU,1-anchorV);
    }
    const int U=160,V=160;
    struct MeshVertex { float position[4],color[3],uv[2]; };
    float key[8]={roll,anchorV,sx,sy,cx,cy,scale,cameraDistance};
    GLint oldArrayBuffer,oldIndexBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&oldIndexBuffer);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    if (!meshBuffer || std::memcmp(key,meshKey,sizeof(key))!=0) {
        if (!meshBuffer) glGenBuffers(1,&meshBuffer);
        std::vector<MeshVertex> vertices((U+1)*(V+1));
        for (int j=0;j<=V;++j) for (int i=0;i<=U;++i) {
            float du=float(i)/U-0.5f, dv=float(j)/V-0.5f;
            auto p=TorusGeometry::focusedPoint(du,dv,roll,anchorV);
            float a=du*2*pi*major, b=pa+dv*2*pi*minor;
            float nx=std::sin(a)*std::cos(b), ny=std::sin(b), nz=std::cos(a)*std::cos(b);
            float ry=ny*std::cos(pa)-nz*std::sin(pa);
            float rz=ny*std::sin(pa)+nz*std::cos(pa);
            float light=mix(1,0.48f+0.52f*clamp(-nx*0.35f-ry*0.45f+rz*0.82f,0,1),roll);
            float w=1-p.z*roll/cameraDistance;
            vertices[j*(U+1)+i]={{cx*w+p.x*sx,cy*w+p.y*sy,p.z*scale,w},
                {light,light,light},{du,-dv}};
        }
        glBindBuffer(GL_ARRAY_BUFFER,meshBuffer);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(MeshVertex),vertices.data(),GL_DYNAMIC_DRAW);
        std::memcpy(meshKey,key,sizeof(key));
    }
    if (!indexBuffer) {
        std::vector<unsigned> indices;
        indices.reserve(U*V*6);
        for (int j=0;j<V;++j) for (int i=0;i<U;++i) {
            unsigned a=j*(U+1)+i,b=a+U+1;
            indices.insert(indices.end(),{a,b,a+1,a+1,b,b+1});
        }
        glGenBuffers(1,&indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(unsigned),indices.data(),GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER,meshBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(4,GL_FLOAT,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(MeshVertex,position)));
    glColorPointer(3,GL_FLOAT,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(MeshVertex,color)));
    glTexCoordPointer(2,GL_FLOAT,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(MeshVertex,uv)));
    // Geometry stays on the GPU while stationary. Longitude navigation only
    // changes a uniform; latitude or unfolding rebuilds one shared vertex grid.
    if (!material) {
        glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
        glTranslatef(anchorU,1-anchorV,0);
    }
    glDrawElements(GL_TRIANGLES,U*V*6,GL_UNSIGNED_INT,nullptr);
    if (!material) { glMatrixMode(GL_TEXTURE); glPopMatrix(); glMatrixMode(GL_MODELVIEW); }
    glPopClientAttrib();
    glBindBuffer(GL_ARRAY_BUFFER,oldArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,oldIndexBuffer);
    glUseProgram(oldProgram);
    glPopAttrib();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(oldMatrixMode);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
#endif
}
