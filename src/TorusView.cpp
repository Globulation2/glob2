#include "TorusView.h"
#include "TorusGeometry.h"
#include "Game.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <algorithm>
#include <cmath>
#include <vector>
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
        "void main(){gl_Position=ftransform();uv=gl_MultiTexCoord0.xy;light=gl_Color.rgb;}\n";
    const char *fragment =
        "#version 120\n"
        "uniform sampler2D world; uniform float fold;\n"
        "varying vec2 uv; varying vec3 light;\n"
        "void main(){\n"
        " vec3 terrain=texture2D(world,uv).rgb;\n"
        " float seen=smoothstep(0.008,0.045,max(terrain.r,max(terrain.g,terrain.b)));\n"
        " vec2 grid=abs(fract(uv*vec2(48.0,24.0)-0.5)-0.5);\n"
        " float line=1.0-smoothstep(0.0,0.035,min(grid.x,grid.y));\n"
        " vec3 unknown=vec3(0.22,0.31,0.41)+line*vec3(0.035,0.045,0.055);\n"
        " vec3 surface=mix(terrain,unknown,(1.0-seen)*fold);\n"
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

TorusView::TorusView() : target(false), dragging(false), amount(0), yaw(0), pitch(TorusGeometry::defaultTilt), zoom(1),
    lastFrame(0), lastCapture(0), texture(0), framebuffer(0), material(0), failed(false), originX(0), originY(0), focusU(0.5f), focusV(0.5f) {}
TorusView::~TorusView()
{
#ifdef HAVE_OPENGL
    if (SDL_GL_GetCurrentContext()) {
        if (material) glDeleteProgram(material);
        if (texture) glDeleteTextures(1, &texture);
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
void TorusView::resetCamera() { yaw = 0; pitch = TorusGeometry::defaultTilt; zoom = 1; }
bool TorusView::event(const SDL_Event &e, int width)
{
    if (!active()) return false;
    if (e.type == SDL_MOUSEBUTTONUP && dragging) { dragging = false; return true; }
    if (e.type == SDL_MOUSEMOTION) {
        if (!(e.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK))) dragging = false;
        if (dragging) { yaw += e.motion.xrel*0.008f; pitch += e.motion.yrel*0.008f; }
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
            zoom = clamp(zoom * std::pow(1.12f, float(e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1))), 0.4f, 3.0f);
            return true;
        }
    }
    return false;
}

void TorusView::draw(Game &game, int team, unsigned options, int vx, int vy, int width, int height)
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
        originX = focus.originX; originY = focus.originY;
        focusU = focus.u; focusV = focus.v;
        lastCapture = 0;
    }
    amount = clamp(amount + (target ? dt : -dt)/1.8f, 0, 1);
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
    const int size = 2048;
    if (!texture) {
        glPushAttrib(GL_TEXTURE_BIT);
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
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
        material = createMaterial();
    }
    if (!lastCapture || now-lastCapture >= 100) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, size, size);
        glOrtho(0, game.map.getW()*32, game.map.getH()*32, 0, -1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // Clouds and offscreen selection arrows are viewport effects; exclude
        // them from the atlas. Fog remains governed by the normal draw options.
        unsigned saved = globalContainer->settings.optionFlags;
        globalContainer->settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
        game.drawMap(0, 0, game.map.getW()*32, game.map.getH()*32, 0, 0, originX, originY, team, options);
        globalContainer->settings.optionFlags = saved;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    float scale = std::min(width/10.0f, height/9.0f) * mix(1, zoom, roll);
    float sx = std::exp(mix(std::log(game.map.getW()*32/(8*pi)), std::log(scale), pull));
    float sy = sx*TorusGeometry::verticalScale(focusU, focusV, roll, aspect);
    float cx = width*0.5f, cy = (height+16)*0.5f;
    float major = smooth(roll), minor = smooth(roll/0.85f);
    TorusGeometry::CameraAngles camera = TorusGeometry::lockedCamera(focusU, focusV, roll, aspect);
    float ya = camera.yaw+yaw*roll;
    float pa = camera.pitch+(pitch-TorusGeometry::defaultTilt)*roll;
    TorusGeometry::Point focus = TorusGeometry::point(focusU, focusV, roll, aspect);
    drawSky(ya, pa, pull, width, height);
    if (material) {
        glUseProgram(material);
        glUniform1i(glGetUniformLocation(material, "world"), 0);
        glUniform1f(glGetUniformLocation(material, "fold"), pull);
    }
    const int U = 160, V = 160;
    auto vertex = [&](int i, int j) {
        float u = float(i)/U, v = TorusGeometry::meshV(float(j)/V, aspect);
        TorusGeometry::Point point = TorusGeometry::point(u, v, roll, aspect);
        // Subtract before rotating: the same game-world patch remains under
        // the center of the camera throughout rolling and unrolling.
        float x = point.x-focus.x, y = point.y-focus.y, z = point.z-focus.z;
        float xx = x*std::cos(ya)+z*std::sin(ya);
        float zz = -x*std::sin(ya)+z*std::cos(ya);
        float yy = y*std::cos(pa)-zz*std::sin(pa);
        zz = y*std::sin(pa)+zz*std::cos(pa);
        float perspective = 18/(18-zz*roll);
        float a = (u-0.5f)*2*pi*major, b = TorusGeometry::latitude(v, aspect, roll)*minor;
        float nx = std::sin(a)*std::cos(b), ny = std::sin(b), nz = std::cos(a)*std::cos(b);
        float rx = nx*std::cos(ya)+nz*std::sin(ya);
        float rz = -nx*std::sin(ya)+nz*std::cos(ya);
        float ry = ny*std::cos(pa)-rz*std::sin(pa);
        rz = ny*std::sin(pa)+rz*std::cos(pa);
        float light = mix(1, 0.48f+0.52f*clamp(-rx*0.35f-ry*0.45f+rz*0.82f, 0, 1), roll);
        glColor3f(light, light, light);
        glTexCoord2f(u, 1-v);
        glVertex3f(cx+xx*sx*perspective, cy+yy*sy*perspective, zz*scale);
    };
    for (int j=0; j<V; ++j) {
        glBegin(GL_QUAD_STRIP);
        for (int i=0; i<=U; ++i) { vertex(i,j); vertex(i,j+1); }
        glEnd();
    }
    glUseProgram(oldProgram);
    glPopAttrib();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(oldMatrixMode);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
#endif
}
