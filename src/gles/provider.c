#include "openconsole/gles_provider.h"

#define OC_SHADER_LIMIT 4u
#define OC_PROGRAM_LIMIT 2u
#define OC_SOURCE_LIMIT 512u

typedef struct {
    GLenum type;
    uint8_t used, compiled, deleted;
    char normalized[OC_SOURCE_LIMIT];
    char log[80];
} oc_shader;

typedef struct {
    uint8_t used, linked, deleted;
    GLuint vertex, fragment;
    char log[80];
} oc_program;

typedef struct {
    const GLfloat *pointer;
    GLint size;
    GLsizei stride;
    uint8_t enabled;
} oc_attribute;

struct OC_GLES_CONTEXT {
    OC_GLES_PROVIDER *owner;
    uint32_t width, height;
    uint8_t *pixels;
    UINTN pixel_bytes;
    GLenum error;
    GLint viewport_x, viewport_y;
    GLsizei viewport_width, viewport_height;
    uint8_t clear[4];
    oc_shader shaders[OC_SHADER_LIMIT];
    oc_program programs[OC_PROGRAM_LIMIT];
    GLuint program;
    oc_attribute attributes[2];
};

static OC_GLES_PROVIDER *active_provider;

static int text_equal(const char *a, const char *b) {
    while (*a != 0 && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static UINTN text_length(const char *s) {
    UINTN n = 0u;
    while (s[n] != 0) ++n;
    return n;
}

static void copy_log(char *out, UINTN capacity, const char *text) {
    UINTN i = 0u;
    if (capacity == 0u) return;
    while (text[i] != 0 && i + 1u < capacity) { out[i] = text[i]; ++i; }
    out[i] = 0;
}

static void set_error(OC_GLES_CONTEXT *context, GLenum error) {
    if (context != 0 && context->error == GL_NO_ERROR) context->error = error;
}

static OC_GLES_CONTEXT *current(void) {
    return active_provider == 0 ? 0 : active_provider->current;
}

static uint8_t channel(GLfloat value) {
    if (value <= 0.0f) return 0u;
    if (value >= 1.0f) return 255u;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static GLenum OC_GL_API oc_gl_get_error(void) {
    OC_GLES_CONTEXT *c = current();
    GLenum value;
    if (c == 0) return GL_INVALID_OPERATION;
    value = c->error;
    c->error = GL_NO_ERROR;
    return value;
}

static const GLubyte *OC_GL_API oc_gl_get_string(GLenum name) {
    static const GLubyte vendor[] = "OpenConsole";
    static const GLubyte renderer[] = "OpenConsole Software GLES subset (non-conformant)";
    static const GLubyte version[] = "OpenGL ES 2.0 OpenConsole ABI 0.1 subset";
    static const GLubyte extensions[] = "";
    OC_GLES_CONTEXT *c = current();
    if (c == 0) return 0;
    if (name == GL_VENDOR) return vendor;
    if (name == GL_RENDERER) return renderer;
    if (name == GL_VERSION) return version;
    if (name == GL_EXTENSIONS) return extensions;
    set_error(c, GL_INVALID_ENUM);
    return 0;
}

static void OC_GL_API oc_gl_viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    OC_GLES_CONTEXT *c = current();
    if (c == 0) return;
    if (width < 0 || height < 0) { set_error(c, GL_INVALID_VALUE); return; }
    c->viewport_x = x; c->viewport_y = y;
    c->viewport_width = width; c->viewport_height = height;
}

static void OC_GL_API oc_gl_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    OC_GLES_CONTEXT *c = current();
    if (c == 0) return;
    c->clear[0] = channel(b); c->clear[1] = channel(g);
    c->clear[2] = channel(r); c->clear[3] = channel(a);
}

static void OC_GL_API oc_gl_clear(uint32_t mask) {
    OC_GLES_CONTEXT *c = current();
    UINTN i;
    if (c == 0) return;
    if ((mask & ~GL_COLOR_BUFFER_BIT) != 0u) { set_error(c, GL_INVALID_VALUE); return; }
    if ((mask & GL_COLOR_BUFFER_BIT) == 0u) return;
    {
        uint32_t pixel=(uint32_t)c->clear[0]|((uint32_t)c->clear[1]<<8)|
                       ((uint32_t)c->clear[2]<<16)|((uint32_t)c->clear[3]<<24);
        uint64_t pair=(uint64_t)pixel|((uint64_t)pixel<<32);
        uint64_t *out=(uint64_t *)(void *)c->pixels;
        for(i=0u;i<c->pixel_bytes/8u;++i)out[i]=pair;
        if((c->pixel_bytes&4u)!=0u)((uint32_t *)(void *)c->pixels)[c->pixel_bytes/4u-1u]=pixel;
    }
}

static oc_shader *shader(OC_GLES_CONTEXT *c, GLuint id) {
    if (id == 0u || id > OC_SHADER_LIMIT || !c->shaders[id - 1u].used) return 0;
    return &c->shaders[id - 1u];
}

static oc_program *program(OC_GLES_CONTEXT *c, GLuint id) {
    if (id == 0u || id > OC_PROGRAM_LIMIT || !c->programs[id - 1u].used) return 0;
    return &c->programs[id - 1u];
}

static GLuint OC_GL_API oc_gl_create_shader(GLenum type) {
    OC_GLES_CONTEXT *c = current();
    GLuint i;
    if (c == 0) return 0u;
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        set_error(c, GL_INVALID_ENUM); return 0u;
    }
    for (i = 0u; i < OC_SHADER_LIMIT; ++i) if (!c->shaders[i].used) {
        c->shaders[i].used = 1u; c->shaders[i].type = type; return i + 1u;
    }
    set_error(c, GL_OUT_OF_MEMORY); return 0u;
}

static int normalize_sources(char *out, UINTN capacity, GLsizei count,
                             const GLchar *const *strings, const GLint *lengths) {
    UINTN n = 0u;
    GLsizei part;
    int comment = 0;
    if (count <= 0 || strings == 0) return 0;
    for (part = 0; part < count; ++part) {
        UINTN i = 0u, limit;
        if (strings[part] == 0) return 0;
        limit = lengths != 0 && lengths[part] >= 0 ? (UINTN)lengths[part] : text_length(strings[part]);
        while (i < limit) {
            char ch = strings[part][i++];
            if (comment == 1) { if (ch == '\n' || ch == '\r') comment = 0; continue; }
            if (comment == 2) {
                if (ch == '*' && i < limit && strings[part][i] == '/') { ++i; comment = 0; }
                continue;
            }
            if (ch == '/' && i < limit && strings[part][i] == '/') { ++i; comment = 1; continue; }
            if (ch == '/' && i < limit && strings[part][i] == '*') { ++i; comment = 2; continue; }
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
            if (n + 1u >= capacity) return 0;
            out[n++] = ch;
        }
    }
    if (comment == 2) return 0;
    out[n] = 0;
    return 1;
}

static void OC_GL_API oc_gl_shader_source(GLuint id, GLsizei count,
                                           const GLchar *const *strings, const GLint *lengths) {
    OC_GLES_CONTEXT *c = current(); oc_shader *s;
    if (c == 0) return;
    s = shader(c, id);
    if (s == 0) { set_error(c, GL_INVALID_VALUE); return; }
    s->compiled = 0u; s->log[0] = 0;
    if (!normalize_sources(s->normalized, sizeof(s->normalized), count, strings, lengths)) {
        s->normalized[0] = 0; copy_log(s->log, sizeof(s->log), "shader source is invalid or exceeds 511 tokens");
    }
}

static void OC_GL_API oc_gl_compile_shader(GLuint id) {
    char expected[OC_SOURCE_LIMIT];
    OC_GLES_CONTEXT *c = current(); oc_shader *s;
    const GLchar *template_source;
    if (c == 0) return;
    s = shader(c, id);
    if (s == 0) { set_error(c, GL_INVALID_VALUE); return; }
    template_source = s->type == GL_VERTEX_SHADER ? OC_GLES_VERTEX_SHADER_TEMPLATE : OC_GLES_FRAGMENT_SHADER_TEMPLATE;
    (void)normalize_sources(expected, sizeof(expected), 1, &template_source, 0);
    s->compiled = text_equal(s->normalized, expected) ? 1u : 0u;
    copy_log(s->log, sizeof(s->log), s->compiled ? "" : "unsupported shader: ABI 0.1 accepts only the documented template");
}

static void get_object_log(const char *log, GLsizei capacity, GLsizei *written, GLchar *out) {
    GLsizei n = 0;
    if (out != 0 && capacity > 0) {
        while (log[n] != 0 && n + 1 < capacity) { out[n] = log[n]; ++n; }
        out[n] = 0;
    }
    if (written != 0) *written = n;
}

static void OC_GL_API oc_gl_get_shader_iv(GLuint id, GLenum pname, GLint *value) {
    OC_GLES_CONTEXT *c = current(); oc_shader *s;
    if (c == 0) return;
    s = shader(c, id);
    if (s == 0 || value == 0) { set_error(c, GL_INVALID_VALUE); return; }
    if (pname == GL_COMPILE_STATUS) *value = s->compiled;
    else if (pname == GL_INFO_LOG_LENGTH) *value = (GLint)text_length(s->log) + 1;
    else set_error(c, GL_INVALID_ENUM);
}

static void OC_GL_API oc_gl_get_shader_log(GLuint id, GLsizei capacity, GLsizei *written, GLchar *out) {
    OC_GLES_CONTEXT *c = current(); oc_shader *s;
    if (c == 0) return;
    s = shader(c, id);
    if (s == 0 || capacity < 0) { set_error(c, GL_INVALID_VALUE); return; }
    get_object_log(s->log, capacity, written, out);
}

static void OC_GL_API oc_gl_delete_shader(GLuint id) {
    OC_GLES_CONTEXT *c = current(); oc_shader *s;
    if (c == 0 || id == 0u) return;
    s = shader(c, id); if (s == 0) { set_error(c, GL_INVALID_VALUE); return; }
    s->deleted = 1u;
}

static GLuint OC_GL_API oc_gl_create_program(void) {
    OC_GLES_CONTEXT *c = current(); GLuint i;
    if (c == 0) return 0u;
    for (i = 0u; i < OC_PROGRAM_LIMIT; ++i) if (!c->programs[i].used) {
        c->programs[i].used = 1u; return i + 1u;
    }
    set_error(c, GL_OUT_OF_MEMORY); return 0u;
}

static void OC_GL_API oc_gl_attach_shader(GLuint pid, GLuint sid) {
    OC_GLES_CONTEXT *c = current(); oc_program *p; oc_shader *s;
    if (c == 0) return;
    p = program(c, pid); s = shader(c, sid);
    if (p == 0 || s == 0) { set_error(c, GL_INVALID_VALUE); return; }
    if (s->type == GL_VERTEX_SHADER) p->vertex = sid; else p->fragment = sid;
}

static void OC_GL_API oc_gl_link_program(GLuint id) {
    OC_GLES_CONTEXT *c = current(); oc_program *p; oc_shader *vs, *fs;
    if (c == 0) return;
    p = program(c, id); if (p == 0) { set_error(c, GL_INVALID_VALUE); return; }
    vs = shader(c, p->vertex); fs = shader(c, p->fragment);
    p->linked = vs != 0 && fs != 0 && vs->compiled && fs->compiled;
    copy_log(p->log, sizeof(p->log), p->linked ? "" : "program requires both compiled ABI 0.1 shader templates");
}

static void OC_GL_API oc_gl_get_program_iv(GLuint id, GLenum pname, GLint *value) {
    OC_GLES_CONTEXT *c = current(); oc_program *p;
    if (c == 0) return;
    p = program(c, id); if (p == 0 || value == 0) { set_error(c, GL_INVALID_VALUE); return; }
    if (pname == GL_LINK_STATUS) *value = p->linked;
    else if (pname == GL_INFO_LOG_LENGTH) *value = (GLint)text_length(p->log) + 1;
    else set_error(c, GL_INVALID_ENUM);
}

static void OC_GL_API oc_gl_get_program_log(GLuint id, GLsizei capacity, GLsizei *written, GLchar *out) {
    OC_GLES_CONTEXT *c = current(); oc_program *p;
    if (c == 0) return;
    p = program(c, id); if (p == 0 || capacity < 0) { set_error(c, GL_INVALID_VALUE); return; }
    get_object_log(p->log, capacity, written, out);
}

static void OC_GL_API oc_gl_delete_program(GLuint id) {
    OC_GLES_CONTEXT *c = current(); oc_program *p;
    if (c == 0 || id == 0u) return;
    p = program(c, id); if (p == 0) { set_error(c, GL_INVALID_VALUE); return; }
    p->deleted = 1u; if (c->program == id) c->program = 0u;
}

static void OC_GL_API oc_gl_use_program(GLuint id) {
    OC_GLES_CONTEXT *c = current(); oc_program *p;
    if (c == 0) return;
    if (id == 0u) { c->program = 0u; return; }
    p = program(c, id); if (p == 0 || !p->linked) { set_error(c, GL_INVALID_OPERATION); return; }
    c->program = id;
}

static GLint OC_GL_API oc_gl_get_attrib_location(GLuint id, const GLchar *name) {
    OC_GLES_CONTEXT *c = current(); oc_program *p;
    if (c == 0) return -1;
    p = program(c, id); if (p == 0 || !p->linked || name == 0) { set_error(c, GL_INVALID_OPERATION); return -1; }
    if (text_equal(name, "a_position")) return 0;
    if (text_equal(name, "a_color")) return 1;
    return -1;
}

static void OC_GL_API oc_gl_vertex_attrib_pointer(GLuint index, GLint size, GLenum type,
                                                   GLboolean normalized, GLsizei stride,
                                                   const GLvoid *pointer) {
    OC_GLES_CONTEXT *c = current();
    if (c == 0) return;
    if (index > 1u || pointer == 0 || type != GL_FLOAT || normalized != GL_FALSE ||
        stride < 0 || (index == 0u ? size != 2 : size != 3)) {
        set_error(c, index > 1u || size < 1 ? GL_INVALID_VALUE : GL_INVALID_OPERATION); return;
    }
    c->attributes[index].pointer = pointer; c->attributes[index].size = size;
    c->attributes[index].stride = stride;
}

static void OC_GL_API oc_gl_enable_vertex_attrib_array(GLuint index) {
    OC_GLES_CONTEXT *c = current();
    if (c == 0) return;
    if (index > 1u) { set_error(c, GL_INVALID_VALUE); return; }
    c->attributes[index].enabled = 1u;
}

static const GLfloat *attribute_vertex(const oc_attribute *a, int vertex) {
    UINTN stride = a->stride == 0 ? (UINTN)a->size * sizeof(GLfloat) : (UINTN)a->stride;
    return (const GLfloat *)((const uint8_t *)a->pointer + (UINTN)vertex * stride);
}

static int64_t edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
    return (int64_t)(px - ax) * (by - ay) - (int64_t)(py - ay) * (bx - ax);
}

static void OC_GL_API oc_gl_draw_arrays(GLenum mode, GLint first, GLsizei count) {
    OC_GLES_CONTEXT *c = current();
    int32_t x[3], y[3]; uint8_t color[3][3]; int v;
    int32_t min_x, max_x, min_y, max_y, px, py; int64_t area;
    if (c == 0) return;
    if (mode != GL_TRIANGLES) { set_error(c, GL_INVALID_ENUM); return; }
    if (first != 0 || count != 3 || c->program == 0u ||
        !c->attributes[0].enabled || !c->attributes[1].enabled ||
        c->attributes[0].pointer == 0 || c->attributes[1].pointer == 0) {
        set_error(c, GL_INVALID_OPERATION); return;
    }
    for (v = 0; v < 3; ++v) {
        const GLfloat *p = attribute_vertex(&c->attributes[0], v);
        const GLfloat *col = attribute_vertex(&c->attributes[1], v);
        if (p[0] < -1.0f || p[0] > 1.0f || p[1] < -1.0f || p[1] > 1.0f) {
            set_error(c, GL_INVALID_OPERATION); return;
        }
        x[v] = (c->viewport_x * 256) + (int32_t)((p[0] + 1.0f) * (GLfloat)c->viewport_width * 128.0f);
        y[v] = (c->viewport_y * 256) + (int32_t)((1.0f - p[1]) * (GLfloat)c->viewport_height * 128.0f);
        color[v][0] = channel(col[0]); color[v][1] = channel(col[1]); color[v][2] = channel(col[2]);
    }
    area = edge(x[0], y[0], x[1], y[1], x[2], y[2]);
    if (area == 0) return;
    if (area < 0) {
        int32_t tx = x[1], ty = y[1]; uint8_t tc[3]; int k;
        x[1] = x[2]; y[1] = y[2]; x[2] = tx; y[2] = ty;
        for (k = 0; k < 3; ++k) { tc[k] = color[1][k]; color[1][k] = color[2][k]; color[2][k] = tc[k]; }
        area = -area;
    }
    min_x = x[0]; max_x = x[0]; min_y = y[0]; max_y = y[0];
    for (v = 1; v < 3; ++v) { if (x[v] < min_x) min_x=x[v]; if (x[v]>max_x) max_x=x[v]; if(y[v]<min_y)min_y=y[v]; if(y[v]>max_y)max_y=y[v]; }
    min_x >>= 8; min_y >>= 8; max_x = (max_x + 255) >> 8; max_y = (max_y + 255) >> 8;
    if (min_x < 0) min_x=0;
    if (min_y < 0) min_y=0;
    if (max_x > (int32_t)c->width) max_x=(int32_t)c->width;
    if (max_y > (int32_t)c->height) max_y=(int32_t)c->height;
    {
        int64_t wdx[3]={256ll*(y[2]-y[1]),256ll*(y[0]-y[2]),256ll*(y[1]-y[0])};
        int64_t wdy[3]={-256ll*(x[2]-x[1]),-256ll*(x[0]-x[2]),-256ll*(x[1]-x[0])};
        int32_t sx=min_x*256+128,sy=min_y*256+128;
        int64_t wr[3]={edge(x[1],y[1],x[2],y[2],sx,sy),
                       edge(x[2],y[2],x[0],y[0],sx,sy),
                       edge(x[0],y[0],x[1],y[1],sx,sy)};
        int64_t cr[3],cdx[3],cdy[3]; int k;
        for(k=0;k<3;++k){
            cr[k]=(wr[0]*color[0][k]+wr[1]*color[1][k]+wr[2]*color[2][k])*65536ll/area;
            cdx[k]=(wdx[0]*color[0][k]+wdx[1]*color[1][k]+wdx[2]*color[2][k])*65536ll/area;
            cdy[k]=(wdy[0]*color[0][k]+wdy[1]*color[1][k]+wdy[2]*color[2][k])*65536ll/area;
        }
        for(py=min_y;py<max_y;++py){
            int64_t w0=wr[0],w1=wr[1],w2=wr[2];
            int64_t cq[3]={cr[0],cr[1],cr[2]};
            for(px=min_x;px<max_x;++px){
                if(w0>=0&&w1>=0&&w2>=0){
                    UINTN o=((UINTN)py*c->width+(UINTN)px)*4u;
                    c->pixels[o]=cq[2]<0?0u:cq[2]>(255ll<<16)?255u:(uint8_t)(cq[2]>>16);
                    c->pixels[o+1u]=cq[1]<0?0u:cq[1]>(255ll<<16)?255u:(uint8_t)(cq[1]>>16);
                    c->pixels[o+2u]=cq[0]<0?0u:cq[0]>(255ll<<16)?255u:(uint8_t)(cq[0]>>16);
                    c->pixels[o+3u]=255u;
                }
                w0+=wdx[0];w1+=wdx[1];w2+=wdx[2];
                cq[0]+=cdx[0];cq[1]+=cdx[1];cq[2]+=cdx[2];
            }
            wr[0]+=wdy[0];wr[1]+=wdy[1];wr[2]+=wdy[2];
            cr[0]+=cdy[0];cr[1]+=cdy[1];cr[2]+=cdy[2];
        }
    }
}

static EFI_STATUS EFIAPI query_capabilities(OC_GLES_CAPABILITIES *caps) {
    if (active_provider == 0 || caps == 0 || caps->structure_size < sizeof(*caps)) return EFI_INVALID_PARAMETER;
    caps->abi_version=OC_GLES_ABI_VERSION; caps->backend_flags=OC_GLES_CONTEXT_SOFTWARE;
    caps->max_width=OC_GLES_MAX_DIMENSION; caps->max_height=OC_GLES_MAX_DIMENSION;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI create_context(const OC_GLES_CONTEXT_DESC *desc, OC_GLES_CONTEXT **out) {
    OC_GLES_CONTEXT *c; uint32_t width, height; UINTN bytes; EFI_STATUS status;
    if (active_provider == 0 || desc == 0 || out == 0 || desc->structure_size < sizeof(*desc)) return EFI_INVALID_PARAMETER;
    *out=0;
    if ((desc->requested_abi >> 16) != OC_GLES_ABI_MAJOR) return EFI_UNSUPPORTED;
    if ((desc->flags & OC_GLES_CONTEXT_ACCELERATED) != 0u) return EFI_UNSUPPORTED;
    width=desc->width==0u?active_provider->platform.display_width:desc->width;
    height=desc->height==0u?active_provider->platform.display_height:desc->height;
    if(width==0u||height==0u||width>OC_GLES_MAX_DIMENSION||height>OC_GLES_MAX_DIMENSION||
       width>OC_GLES_MAX_TARGET_BYTES/4u/height) return EFI_INVALID_PARAMETER;
    bytes=(UINTN)width*height*4u;
    if (width!=active_provider->platform.display_width||height!=active_provider->platform.display_height) return EFI_UNSUPPORTED;
    status=active_provider->platform.allocate(active_provider->platform.user,sizeof(*c),(void**)&c);
    if(EFI_ERROR(status)) return status;
    { UINTN i; uint8_t *p=(uint8_t*)c; for(i=0;i<sizeof(*c);++i)p[i]=0; }
    status=active_provider->platform.allocate(active_provider->platform.user,bytes,(void**)&c->pixels);
    if(EFI_ERROR(status)){active_provider->platform.release(active_provider->platform.user,c);return status;}
    c->owner=active_provider;c->width=width;c->height=height;c->pixel_bytes=bytes;
    c->viewport_width=(GLsizei)width;c->viewport_height=(GLsizei)height;
    active_provider->outstanding_bytes+=sizeof(*c)+bytes;active_provider->live_contexts++;
    *out=c;return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI destroy_context(OC_GLES_CONTEXT *c) {
    OC_GLES_PROVIDER *p;
    if(active_provider==0||c==0||c->owner!=active_provider)return EFI_INVALID_PARAMETER;
    p=c->owner;if(p->current==c)p->current=0;
    p->outstanding_bytes-=sizeof(*c)+c->pixel_bytes;p->live_contexts--;
    p->platform.release(p->platform.user,c->pixels);p->platform.release(p->platform.user,c);return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI make_current(OC_GLES_CONTEXT *c) {
    if(active_provider==0)return EFI_NOT_FOUND;
    if(c!=0&&c->owner!=active_provider)return EFI_INVALID_PARAMETER;
    active_provider->current=c;return EFI_SUCCESS;
}

static OC_GLES_PROC EFIAPI get_proc_address(const char *name) {
    if(name==0)return 0;
#define OC_PROC(n, f) if(text_equal(name,n)) return (OC_GLES_PROC)(f)
    OC_PROC("glGetError",oc_gl_get_error); OC_PROC("glGetString",oc_gl_get_string);
    OC_PROC("glViewport",oc_gl_viewport); OC_PROC("glClearColor",oc_gl_clear_color); OC_PROC("glClear",oc_gl_clear);
    OC_PROC("glCreateShader",oc_gl_create_shader); OC_PROC("glShaderSource",oc_gl_shader_source); OC_PROC("glCompileShader",oc_gl_compile_shader);
    OC_PROC("glGetShaderiv",oc_gl_get_shader_iv); OC_PROC("glGetShaderInfoLog",oc_gl_get_shader_log); OC_PROC("glDeleteShader",oc_gl_delete_shader);
    OC_PROC("glCreateProgram",oc_gl_create_program); OC_PROC("glAttachShader",oc_gl_attach_shader); OC_PROC("glLinkProgram",oc_gl_link_program);
    OC_PROC("glGetProgramiv",oc_gl_get_program_iv); OC_PROC("glGetProgramInfoLog",oc_gl_get_program_log); OC_PROC("glDeleteProgram",oc_gl_delete_program);
    OC_PROC("glUseProgram",oc_gl_use_program); OC_PROC("glGetAttribLocation",oc_gl_get_attrib_location);
    OC_PROC("glVertexAttribPointer",oc_gl_vertex_attrib_pointer); OC_PROC("glEnableVertexAttribArray",oc_gl_enable_vertex_attrib_array);
    OC_PROC("glDrawArrays",oc_gl_draw_arrays);
#undef OC_PROC
    return 0;
}

static EFI_STATUS EFIAPI present(OC_GLES_CONTEXT *c) {
    if(active_provider==0||c==0||c->owner!=active_provider)return EFI_INVALID_PARAMETER;
    return active_provider->platform.blit_bgra(active_provider->platform.user,c->pixels,c->width,c->height);
}

EFI_STATUS oc_gles_provider_initialize(OC_GLES_PROVIDER *provider, const OC_GLES_PLATFORM_SERVICES *platform) {
    UINTN i; uint8_t *p;
    if(provider==0||platform==0||platform->allocate==0||platform->release==0||platform->blit_bgra==0)return EFI_INVALID_PARAMETER;
    if(active_provider!=0&&active_provider!=provider)return EFI_ALREADY_STARTED;
    p=(uint8_t*)provider;for(i=0;i<sizeof(*provider);++i)p[i]=0;
    provider->platform=*platform;provider->protocol.structure_size=sizeof(provider->protocol);
    provider->protocol.abi_version=OC_GLES_ABI_VERSION;provider->protocol.query_capabilities=query_capabilities;
    provider->protocol.create_context=create_context;provider->protocol.destroy_context=destroy_context;
    provider->protocol.make_current=make_current;provider->protocol.get_proc_address=get_proc_address;provider->protocol.present=present;
    active_provider=provider;return EFI_SUCCESS;
}

uint64_t oc_gles_provider_outstanding_bytes(const OC_GLES_PROVIDER *provider){return provider==0?0u:provider->outstanding_bytes;}
