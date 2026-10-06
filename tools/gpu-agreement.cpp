#include <SkinModel.h>
#include <SkinDeformation.h>
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <stdexcept>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char **argv)try {
 check(argc==2,"asset path required");
 std::ifstream input(argv[1],std::ios::binary);std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});std::string error;
 auto model=GAGCore::SkinModel::decode(bytes,error);check(bool(model),error.c_str());
 check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
 auto *window=SDL_CreateWindow("Rig parity",128,128,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(window,SDL_GetError());
 auto context=SDL_GL_CreateContext(window);check(context,SDL_GetError());
 std::cout<<"GL_VENDOR="<<glGetString(GL_VENDOR)<<" GL_RENDERER="<<glGetString(GL_RENDERER)<<" GL_VERSION="<<glGetString(GL_VERSION)<<'\n';
 const std::string source=std::string("#version 130\nin vec3 position;in vec3 surfaceNormal;in vec2 texcoord;in vec4 joints;in vec4 weights;out vec2 uv;out vec3 normal;\n")+GAGCore::SkinDeformationGLSL;
 const char *p=source.c_str();GLuint shader=glCreateShader(GL_VERTEX_SHADER);glShaderSource(shader,1,&p,nullptr);glCompileShader(shader);
 GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);char log[4096]{};glGetShaderInfoLog(shader,sizeof(log),nullptr,log);check(ok,log);
 GLuint program=glCreateProgram();glAttachShader(program,shader);
 const char *attributes[]={"position","surfaceNormal","texcoord","joints","weights"};
 for(unsigned i=0;i<5;++i)glBindAttribLocation(program,i,attributes[i]);
 const char *outputs[]={"gl_Position","normal"};glTransformFeedbackVaryings(program,2,outputs,GL_SEPARATE_ATTRIBS);glLinkProgram(program);
 glGetProgramiv(program,GL_LINK_STATUS,&ok);glGetProgramInfoLog(program,sizeof(log),nullptr,log);check(ok,log);glUseProgram(program);
 GLuint vbo,buffers[2];glGenBuffers(1,&vbo);glGenBuffers(2,buffers);
 std::vector<float> vertices(model->vertices()*16);
 for(unsigned v=0;v<model->vertices();++v){
  auto *dest=vertices.data()+v*16;std::copy_n(model->rest().data()+v*6,6,dest);std::copy_n(model->uv().data()+v*2,2,dest+6);
  for(unsigned j=0;j<4;++j){dest[8+j]=model->influences()[v].bones[j];dest[12+j]=model->influences()[v].weights[j];}
 }
 glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*4,vertices.data(),GL_STATIC_DRAW);
 const unsigned sizes[]={3,3,2,4,4},offsets[]={0,3,6,8,12};
 for(unsigned i=0;i<5;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,64,reinterpret_cast<void*>(offsets[i]*sizeof(float)));}
 for(unsigned i=0;i<2;++i){glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[i]);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,model->vertices()*(i?3:4)*sizeof(float),nullptr,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,i,buffers[i]);}
 glEnable(GL_RASTERIZER_DISCARD);
 float maxPosition=0,maxNormal=0;std::vector<float> cpu,positions(model->vertices()*4),normals(model->vertices()*3);
 for(unsigned clip=0;clip<model->clips().size();++clip)for(unsigned frame=0;frame<256;++frame){
  GAGCore::SkinPalette palette;check(model->paletteForFrame(clip,frame,palette),"invalid palette");
  std::array<float,512> matrices{};for(unsigned b=0;b<palette.count;++b)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)matrices[b*16+x*4+y]=palette.positions[b][y*4+x];
  glUniformMatrix4fv(glGetUniformLocation(program,"bones[0]"),palette.count,GL_FALSE,matrices.data());
  const auto &camera=model->clips()[clip];std::array<float,16> view;std::array<float,9> normal;
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)view[x*4+y]=camera.modelToClip[y*4+x];
  for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x)normal[x*3+y]=camera.normalToCamera[y*3+x];
  glUniformMatrix4fv(glGetUniformLocation(program,"view"),1,GL_FALSE,view.data());glUniformMatrix3fv(glGetUniformLocation(program,"normalView"),1,GL_FALSE,normal.data());
  glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,model->vertices());glEndTransformFeedback();
  glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[0]);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,positions.size()*4,positions.data());
  glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[1]);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,normals.size()*4,normals.data());
  check(glGetError()==GL_NO_ERROR,"GL capture failed");check(model->evaluate(clip,frame,cpu),"CPU evaluation failed");
  for(unsigned v=0;v<model->vertices();++v){float squared=0;
   for(unsigned k=0;k<3;++k){const float projected=positions[v*4+k]*(k<2?1.25f:1.f);maxPosition=std::max(maxPosition,std::abs(projected-cpu[v*6+k])*model->logicalSize()/2);const float d=normals[v*3+k]-cpu[v*6+3+k];squared+=d*d;}
   maxNormal=std::max(maxNormal,std::sqrt(squared));
  }
 }
 std::cout<<"vertices="<<model->vertices()<<" max_logical_pixels="<<maxPosition<<" max_normal_vector_error="<<maxNormal<<'\n';
 check(maxPosition<=.05f&&maxNormal<=.001f,"pose agreement budget exceeded");
 SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
