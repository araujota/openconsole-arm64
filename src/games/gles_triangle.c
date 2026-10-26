#include "openconsole/gles2_protocol.h"

static void debug_putc(char value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402)); }
static void debug_write(const char *text) { while (*text != 0) debug_putc(*text++); }

#define RESOLVE(type, name) ((type)gles->get_proc_address(name))

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    OC_GLES2_PROTOCOL *gles=0; OC_GLES_CONTEXT *context=0; EFI_STATUS status;
    OC_GLES_CONTEXT_DESC desc={sizeof(desc),OC_GLES_ABI_VERSION,0u,0u,OC_GLES_CONTEXT_SOFTWARE,{0,0,0}};
    PFNGLCLEARCOLORPROC clear_color; PFNGLCLEARPROC clear; PFNGLCREATESHADERPROC create_shader;
    PFNGLSHADERSOURCEPROC shader_source; PFNGLCOMPILESHADERPROC compile_shader;
    PFNGLGETSHADERIVPROC get_shader_iv; PFNGLCREATEPROGRAMPROC create_program;
    PFNGLATTACHSHADERPROC attach_shader; PFNGLLINKPROGRAMPROC link_program;
    PFNGLGETPROGRAMIVPROC get_program_iv; PFNGLUSEPROGRAMPROC use_program;
    PFNGLGETATTRIBLOCATIONPROC get_attrib; PFNGLVERTEXATTRIBPOINTERPROC attrib;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enable; PFNGLDRAWARRAYSPROC draw;
    const GLchar *vs_source=OC_GLES_VERTEX_SHADER_TEMPLATE,*fs_source=OC_GLES_FRAGMENT_SHADER_TEMPLATE;
    static const GLfloat positions[]={-0.75f,-0.65f,0.75f,-0.65f,0.0f,0.75f};
    static const GLfloat colors[]={1,0,0,0,1,0,0,0,1};
    GLuint vs,fs,program; GLint ok,position,color; EFI_INPUT_KEY key; UINTN index=0u;
    (void)image_handle;
    status=system_table->boot_services->locate_protocol(&OC_GLES2_PROTOCOL_GUID,0,(void**)&gles);
    if(EFI_ERROR(status)||gles->abi_version!=OC_GLES_ABI_VERSION)return EFI_UNSUPPORTED;
    status=gles->create_context(&desc,&context);if(EFI_ERROR(status))return status;
    status=gles->make_current(context);if(EFI_ERROR(status))goto done;
    clear_color=RESOLVE(PFNGLCLEARCOLORPROC,"glClearColor");clear=RESOLVE(PFNGLCLEARPROC,"glClear");
    create_shader=RESOLVE(PFNGLCREATESHADERPROC,"glCreateShader");shader_source=RESOLVE(PFNGLSHADERSOURCEPROC,"glShaderSource");
    compile_shader=RESOLVE(PFNGLCOMPILESHADERPROC,"glCompileShader");get_shader_iv=RESOLVE(PFNGLGETSHADERIVPROC,"glGetShaderiv");
    create_program=RESOLVE(PFNGLCREATEPROGRAMPROC,"glCreateProgram");attach_shader=RESOLVE(PFNGLATTACHSHADERPROC,"glAttachShader");
    link_program=RESOLVE(PFNGLLINKPROGRAMPROC,"glLinkProgram");get_program_iv=RESOLVE(PFNGLGETPROGRAMIVPROC,"glGetProgramiv");
    use_program=RESOLVE(PFNGLUSEPROGRAMPROC,"glUseProgram");get_attrib=RESOLVE(PFNGLGETATTRIBLOCATIONPROC,"glGetAttribLocation");
    attrib=RESOLVE(PFNGLVERTEXATTRIBPOINTERPROC,"glVertexAttribPointer");enable=RESOLVE(PFNGLENABLEVERTEXATTRIBARRAYPROC,"glEnableVertexAttribArray");
    draw=RESOLVE(PFNGLDRAWARRAYSPROC,"glDrawArrays");
    if(clear_color==0||clear==0||create_shader==0||shader_source==0||compile_shader==0||get_shader_iv==0||
       create_program==0||attach_shader==0||link_program==0||get_program_iv==0||use_program==0||get_attrib==0||attrib==0||enable==0||draw==0){status=EFI_UNSUPPORTED;goto done;}
    clear_color(0.04f,0.08f,0.12f,1.0f);clear(GL_COLOR_BUFFER_BIT);
    vs=create_shader(GL_VERTEX_SHADER);fs=create_shader(GL_FRAGMENT_SHADER);
    shader_source(vs,1,&vs_source,0);shader_source(fs,1,&fs_source,0);compile_shader(vs);compile_shader(fs);
    get_shader_iv(vs,GL_COMPILE_STATUS,&ok);if(!ok){status=EFI_UNSUPPORTED;goto done;}
    get_shader_iv(fs,GL_COMPILE_STATUS,&ok);if(!ok){status=EFI_UNSUPPORTED;goto done;}
    program=create_program();attach_shader(program,vs);attach_shader(program,fs);link_program(program);
    get_program_iv(program,GL_LINK_STATUS,&ok);if(!ok){status=EFI_UNSUPPORTED;goto done;}use_program(program);
    position=get_attrib(program,"a_position");color=get_attrib(program,"a_color");
    attrib((GLuint)position,2,GL_FLOAT,GL_FALSE,0,positions);attrib((GLuint)color,3,GL_FLOAT,GL_FALSE,0,colors);
    enable((GLuint)position);enable((GLuint)color);draw(GL_TRIANGLES,0,3);
    status=gles->present(context);if(EFI_ERROR(status))goto done;
    debug_write("OPENCONSOLE_GLES_SCENE_READY\n");
    for(;;){if(!EFI_ERROR(system_table->boot_services->wait_for_event(1u,&system_table->con_in->wait_for_key,&index))&&
                !EFI_ERROR(system_table->con_in->read_key_stroke(system_table->con_in,&key)))break;}
    debug_write("OPENCONSOLE_GLES_KEY_RECEIVED\n");
done:
    (void)gles->make_current(0);if(context!=0){EFI_STATUS destroy=gles->destroy_context(context);if(!EFI_ERROR(status))status=destroy;}
    return status;
}
