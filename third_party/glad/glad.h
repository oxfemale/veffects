#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int8_t GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef uint8_t GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef char GLchar;

typedef void* (*GLADloadproc)(const char *name);

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_POINTS 0x0000
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_RGBA 0x1908
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_COMPONENT24 0x81A6

typedef void (*PFNGLCLEARPROC)(GLbitfield mask);
typedef void (*PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (*PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (*PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (*PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (*PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum type);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (*PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (*PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (*PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (*PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (*PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (*PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (*PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (*PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (*PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const void *indices);
typedef void (*PFNGLLINEWIDTHPROC)(GLfloat width);
typedef void (*PFNGLPOINTSIZEPROC)(GLfloat size);
typedef void (*PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);
typedef void (*PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *ids);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (*PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint *renderbuffers);
typedef void (*PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void (*PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef void (*PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint *renderbuffers);

extern PFNGLCLEARPROC glad_glClear;
extern PFNGLCLEARCOLORPROC glad_glClearColor;
extern PFNGLVIEWPORTPROC glad_glViewport;
extern PFNGLGENBUFFERSPROC glad_glGenBuffers;
extern PFNGLBINDBUFFERPROC glad_glBindBuffer;
extern PFNGLBUFFERDATAPROC glad_glBufferData;
extern PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers;
extern PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer;
extern PFNGLCREATESHADERPROC glad_glCreateShader;
extern PFNGLSHADERSOURCEPROC glad_glShaderSource;
extern PFNGLCOMPILESHADERPROC glad_glCompileShader;
extern PFNGLCREATEPROGRAMPROC glad_glCreateProgram;
extern PFNGLATTACHSHADERPROC glad_glAttachShader;
extern PFNGLLINKPROGRAMPROC glad_glLinkProgram;
extern PFNGLUSEPROGRAMPROC glad_glUseProgram;
extern PFNGLDELETESHADERPROC glad_glDeleteShader;
extern PFNGLDELETEPROGRAMPROC glad_glDeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation;
extern PFNGLUNIFORM1FPROC glad_glUniform1f;
extern PFNGLUNIFORM2FPROC glad_glUniform2f;
extern PFNGLUNIFORM3FPROC glad_glUniform3f;
extern PFNGLUNIFORM4FPROC glad_glUniform4f;
extern PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv;
extern PFNGLDRAWARRAYSPROC glad_glDrawArrays;
extern PFNGLDRAWELEMENTSPROC glad_glDrawElements;
extern PFNGLLINEWIDTHPROC glad_glLineWidth;
extern PFNGLPOINTSIZEPROC glad_glPointSize;
extern PFNGLREADPIXELSPROC glad_glReadPixels;
extern PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer;
extern PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage;
extern PFNGLGETSHADERIVPROC glad_glGetShaderiv;
extern PFNGLGETPROGRAMIVPROC glad_glGetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog;
extern PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog;
extern PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers;
extern PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers;

#define glClear glad_glClear
#define glClearColor glad_glClearColor
#define glViewport glad_glViewport
#define glGenBuffers glad_glGenBuffers
#define glBindBuffer glad_glBindBuffer
#define glBufferData glad_glBufferData
#define glDeleteBuffers glad_glDeleteBuffers
#define glGenVertexArrays glad_glGenVertexArrays
#define glBindVertexArray glad_glBindVertexArray
#define glDeleteVertexArrays glad_glDeleteVertexArrays
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glVertexAttribPointer glad_glVertexAttribPointer
#define glCreateShader glad_glCreateShader
#define glShaderSource glad_glShaderSource
#define glCompileShader glad_glCompileShader
#define glCreateProgram glad_glCreateProgram
#define glAttachShader glad_glAttachShader
#define glLinkProgram glad_glLinkProgram
#define glUseProgram glad_glUseProgram
#define glDeleteShader glad_glDeleteShader
#define glDeleteProgram glad_glDeleteProgram
#define glGetUniformLocation glad_glGetUniformLocation
#define glUniform1f glad_glUniform1f
#define glUniform2f glad_glUniform2f
#define glUniform3f glad_glUniform3f
#define glUniform4f glad_glUniform4f
#define glUniformMatrix4fv glad_glUniformMatrix4fv
#define glDrawArrays glad_glDrawArrays
#define glDrawElements glad_glDrawElements
#define glLineWidth glad_glLineWidth
#define glPointSize glad_glPointSize
#define glReadPixels glad_glReadPixels
#define glGenFramebuffers glad_glGenFramebuffers
#define glBindFramebuffer glad_glBindFramebuffer
#define glFramebufferRenderbuffer glad_glFramebufferRenderbuffer
#define glGenRenderbuffers glad_glGenRenderbuffers
#define glBindRenderbuffer glad_glBindRenderbuffer
#define glRenderbufferStorage glad_glRenderbufferStorage
#define glGetShaderiv glad_glGetShaderiv
#define glGetProgramiv glad_glGetProgramiv
#define glGetShaderInfoLog glad_glGetShaderInfoLog
#define glGetProgramInfoLog glad_glGetProgramInfoLog
#define glDeleteFramebuffers glad_glDeleteFramebuffers
#define glDeleteRenderbuffers glad_glDeleteRenderbuffers

int gladLoadGLLoader(GLADloadproc load);

#ifdef __cplusplus
}
#endif
