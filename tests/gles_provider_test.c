#include "openconsole/gles_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char *last; uint32_t width, height; } fixture;

static EFI_STATUS allocate(void *user, UINTN size, void **out) {
    (void)user; *out = malloc((size_t)size); return *out == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}
static void release(void *user, void *memory) { (void)user; free(memory); }
static EFI_STATUS blit(void *user, const void *pixels, uint32_t width, uint32_t height) {
    fixture *f = user; size_t bytes = (size_t)width * height * 4u;
    free(f->last); f->last = malloc(bytes); if (f->last == NULL) return EFI_OUT_OF_RESOURCES;
    memcpy(f->last, pixels, bytes); f->width=width; f->height=height; return EFI_SUCCESS;
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)

int main(void) {
    fixture f={0}; OC_GLES_PROVIDER provider; OC_GLES_CONTEXT *context=0; unsigned iteration;
    OC_GLES_PLATFORM_SERVICES platform={&f,allocate,release,blit,64u,64u};
    OC_GLES_CONTEXT_DESC desc={sizeof(desc),OC_GLES_ABI_VERSION,64u,64u,OC_GLES_CONTEXT_SOFTWARE,{0,0,0}};
    PFNGLGETERRORPROC get_error; PFNGLCLEARCOLORPROC clear_color; PFNGLCLEARPROC clear;
    PFNGLCREATESHADERPROC create_shader; PFNGLSHADERSOURCEPROC shader_source; PFNGLCOMPILESHADERPROC compile_shader;
    PFNGLGETSHADERIVPROC get_shader_iv; PFNGLCREATEPROGRAMPROC create_program; PFNGLATTACHSHADERPROC attach_shader;
    PFNGLLINKPROGRAMPROC link_program; PFNGLGETPROGRAMIVPROC get_program_iv; PFNGLUSEPROGRAMPROC use_program;
    PFNGLVERTEXATTRIBPOINTERPROC attrib; PFNGLENABLEVERTEXATTRIBARRAYPROC enable; PFNGLDRAWARRAYSPROC draw;
    static const GLfloat positions[]={-0.75f,-0.65f,0.75f,-0.65f,0.0f,0.75f};
    static const GLfloat colors[]={1,0,0,0,1,0,0,0,1};
    const GLchar *vs_source=OC_GLES_VERTEX_SHADER_TEMPLATE,*fs_source=OC_GLES_FRAGMENT_SHADER_TEMPLATE;
    GLuint vs,fs,p; GLint ok;
    CHECK(oc_gles_provider_initialize(&provider,&platform)==EFI_SUCCESS);
    desc.requested_abi=0x00010000u; CHECK(provider.protocol.create_context(&desc,&context)==EFI_UNSUPPORTED); CHECK(context==0);
    desc.requested_abi=OC_GLES_ABI_VERSION; CHECK(provider.protocol.create_context(&desc,&context)==EFI_SUCCESS);
    CHECK(provider.protocol.make_current(context)==EFI_SUCCESS);
    CHECK(provider.protocol.get_proc_address("glTexImage2D")==0);
#define PROC(type,name) ((type)provider.protocol.get_proc_address(name))
    get_error=PROC(PFNGLGETERRORPROC,"glGetError"); clear_color=PROC(PFNGLCLEARCOLORPROC,"glClearColor"); clear=PROC(PFNGLCLEARPROC,"glClear");
    create_shader=PROC(PFNGLCREATESHADERPROC,"glCreateShader"); shader_source=PROC(PFNGLSHADERSOURCEPROC,"glShaderSource");
    compile_shader=PROC(PFNGLCOMPILESHADERPROC,"glCompileShader"); get_shader_iv=PROC(PFNGLGETSHADERIVPROC,"glGetShaderiv");
    create_program=PROC(PFNGLCREATEPROGRAMPROC,"glCreateProgram"); attach_shader=PROC(PFNGLATTACHSHADERPROC,"glAttachShader");
    link_program=PROC(PFNGLLINKPROGRAMPROC,"glLinkProgram"); get_program_iv=PROC(PFNGLGETPROGRAMIVPROC,"glGetProgramiv");
    use_program=PROC(PFNGLUSEPROGRAMPROC,"glUseProgram"); attrib=PROC(PFNGLVERTEXATTRIBPOINTERPROC,"glVertexAttribPointer");
    enable=PROC(PFNGLENABLEVERTEXATTRIBARRAYPROC,"glEnableVertexAttribArray"); draw=PROC(PFNGLDRAWARRAYSPROC,"glDrawArrays");
    clear(1u); CHECK(get_error()==GL_INVALID_VALUE); CHECK(get_error()==GL_NO_ERROR);
    clear_color(0.04f,0.08f,0.12f,1.0f); clear(GL_COLOR_BUFFER_BIT);
    vs=create_shader(GL_VERTEX_SHADER); fs=create_shader(GL_FRAGMENT_SHADER);
    shader_source(vs,1,&vs_source,0); shader_source(fs,1,&fs_source,0); compile_shader(vs); compile_shader(fs);
    get_shader_iv(vs,GL_COMPILE_STATUS,&ok);CHECK(ok==GL_TRUE);get_shader_iv(fs,GL_COMPILE_STATUS,&ok);CHECK(ok==GL_TRUE);
    p=create_program();attach_shader(p,vs);attach_shader(p,fs);link_program(p);get_program_iv(p,GL_LINK_STATUS,&ok);CHECK(ok==GL_TRUE);use_program(p);
    attrib(0,2,GL_FLOAT,GL_FALSE,0,positions);attrib(1,3,GL_FLOAT,GL_FALSE,0,colors);enable(0);enable(1);draw(GL_TRIANGLES,0,3);
    CHECK(get_error()==GL_NO_ERROR);CHECK(provider.protocol.present(context)==EFI_SUCCESS);
    CHECK(f.last[0]==31u && f.last[1]==20u && f.last[2]==10u);
    { size_t center=((size_t)32*64u+32u)*4u; CHECK((unsigned)f.last[center]+f.last[center+1u]+f.last[center+2u]>150u); }
    CHECK(provider.protocol.destroy_context(context)==EFI_SUCCESS);CHECK(oc_gles_provider_outstanding_bytes(&provider)==0u);
    for(iteration=0;iteration<100u;++iteration){CHECK(provider.protocol.create_context(&desc,&context)==EFI_SUCCESS);CHECK(provider.protocol.destroy_context(context)==EFI_SUCCESS);CHECK(provider.outstanding_bytes==0u);}
    desc.width=OC_GLES_MAX_DIMENSION+1u;CHECK(provider.protocol.create_context(&desc,&context)==EFI_INVALID_PARAMETER);
    free(f.last);puts("PASS: GLES ABI, frontend, software rasterizer, and 100-context teardown");return 0;
}
