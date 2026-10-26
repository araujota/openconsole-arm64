#ifndef OPENCONSOLE_GLES2_H
#define OPENCONSOLE_GLES2_H

#include <stdint.h>
#include "openconsole/target.h"

#if OC_TARGET_ARCH_X64
#define OC_GL_API __attribute__((ms_abi))
#else
#define OC_GL_API
#endif

typedef uint32_t GLenum;
typedef uint8_t GLboolean;
typedef int32_t GLint;
typedef int32_t GLsizei;
typedef uint32_t GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef uint8_t GLubyte;
typedef void GLvoid;

#define GL_FALSE 0u
#define GL_TRUE 1u
#define GL_NO_ERROR 0u
#define GL_INVALID_ENUM 0x0500u
#define GL_INVALID_VALUE 0x0501u
#define GL_INVALID_OPERATION 0x0502u
#define GL_OUT_OF_MEMORY 0x0505u
#define GL_COLOR_BUFFER_BIT 0x00004000u
#define GL_FLOAT 0x1406u
#define GL_TRIANGLES 0x0004u
#define GL_VERTEX_SHADER 0x8B31u
#define GL_FRAGMENT_SHADER 0x8B30u
#define GL_COMPILE_STATUS 0x8B81u
#define GL_LINK_STATUS 0x8B82u
#define GL_INFO_LOG_LENGTH 0x8B84u
#define GL_VENDOR 0x1F00u
#define GL_RENDERER 0x1F01u
#define GL_VERSION 0x1F02u
#define GL_EXTENSIONS 0x1F03u

/* These two templates are the complete shader language accepted by ABI 0.1.
 * ASCII whitespace and C/C++ comments may vary; all other tokens are rejected. */
#define OC_GLES_VERTEX_SHADER_TEMPLATE \
    "attribute vec2 a_position;attribute vec3 a_color;varying vec3 v_color;" \
    "void main(){gl_Position=vec4(a_position,0.0,1.0);v_color=a_color;}"
#define OC_GLES_FRAGMENT_SHADER_TEMPLATE \
    "precision mediump float;varying vec3 v_color;" \
    "void main(){gl_FragColor=vec4(v_color,1.0);}"

typedef GLenum (OC_GL_API *PFNGLGETERRORPROC)(void);
typedef const GLubyte *(OC_GL_API *PFNGLGETSTRINGPROC)(GLenum);
typedef void (OC_GL_API *PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void (OC_GL_API *PFNGLCLEARCOLORPROC)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (OC_GL_API *PFNGLCLEARPROC)(uint32_t);
typedef GLuint (OC_GL_API *PFNGLCREATESHADERPROC)(GLenum);
typedef void (OC_GL_API *PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void (OC_GL_API *PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (OC_GL_API *PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void (OC_GL_API *PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (OC_GL_API *PFNGLDELETESHADERPROC)(GLuint);
typedef GLuint (OC_GL_API *PFNGLCREATEPROGRAMPROC)(void);
typedef void (OC_GL_API *PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (OC_GL_API *PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (OC_GL_API *PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint *);
typedef void (OC_GL_API *PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (OC_GL_API *PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void (OC_GL_API *PFNGLUSEPROGRAMPROC)(GLuint);
typedef GLint (OC_GL_API *PFNGLGETATTRIBLOCATIONPROC)(GLuint, const GLchar *);
typedef void (OC_GL_API *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
typedef void (OC_GL_API *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (OC_GL_API *PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);

#endif
