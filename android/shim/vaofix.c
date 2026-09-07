#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <string.h>
#include <android/log.h>
static GLuint g_vao = 0;
static void (*real_glGenVertexArrays)(GLsizei, GLuint*) = 0;
static void (*real_glBindVertexArray)(GLuint) = 0;
static void (*real_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = 0;
static void ensure_vao(void) {
    if (g_vao == 0) {
        if (!real_glGenVertexArrays) real_glGenVertexArrays = dlsym(RTLD_NEXT, "glGenVertexArrays");
        if (real_glGenVertexArrays) real_glGenVertexArrays(1, &g_vao);
    }
    if (g_vao) {
        if (!real_glBindVertexArray) real_glBindVertexArray = dlsym(RTLD_NEXT, "glBindVertexArray");
        GLint cur = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &cur);
        if (cur == 0 && real_glBindVertexArray) real_glBindVertexArray(g_vao);
    }
}
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    if (!real_glVertexAttribPointer) real_glVertexAttribPointer = dlsym(RTLD_NEXT, "glVertexAttribPointer");
    ensure_vao();
    if (real_glVertexAttribPointer) real_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}
void glEnableVertexAttribArray(GLuint index) {
    static void (*real)(GLuint)=0;
    if (!real) real = dlsym(RTLD_NEXT, "glEnableVertexAttribArray");
    ensure_vao();
    if (real) real(index);
}
void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    static void (*real)(GLenum, GLenum, GLint)=0;
    if (!real) real = dlsym(RTLD_NEXT, "glTexParameteri");
    if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && param == GL_REPEAT) param = GL_CLAMP_TO_EDGE;
    if (real) real(target, pname, param);
}
void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    static void (*real)(GLenum, GLenum, GLfloat)=0;
    if (!real) real = dlsym(RTLD_NEXT, "glTexParameterf");
    if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && (GLint)param == GL_REPEAT) param = (GLfloat)GL_CLAMP_TO_EDGE;
    if (real) real(target, pname, param);
}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    static void (*real)(GLint, GLint, GLsizei, GLsizei)=0;
    if (!real) real = dlsym(RTLD_NEXT, "glViewport");
    if (width == 1920 && height == 1080) {
        __android_log_print(ANDROID_LOG_INFO, "vaofix", "glViewport %d %d %d %d -> 0 0 2400 1080", x,y,width,height);
        if (real) real(0,0,2400,1080);
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "vaofix", "glViewport %d %d %d %d", x,y,width,height);
    if (real) real(x,y,width,height);
}
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    static void (*real)(GLint, GLint, GLsizei, GLsizei)=0;
    if (!real) real = dlsym(RTLD_NEXT, "glScissor");
    if (width == 1920 && height == 1080) {
        __android_log_print(ANDROID_LOG_INFO, "vaofix", "glScissor %d %d %d %d -> 0 0 2400 1080", x,y,width,height);
        if (real) real(0,0,2400,1080);
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "vaofix", "glScissor %d %d %d %d", x,y,width,height);
    if (real) real(x,y,width,height);
}
void glEnable(GLenum cap) {
    static void (*real)(GLenum)=0;
    if (!real) real = dlsym(RTLD_NEXT, "glEnable");
    if (cap == GL_SCISSOR_TEST) {
        __android_log_print(ANDROID_LOG_INFO, "vaofix", "glEnable SCISSOR ignored");
        return;
    }
    if (real) real(cap);
}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* procname) {
    static __eglMustCastToProperFunctionPointerType (*real)(const char*)=0;
    if (!real) real = (__eglMustCastToProperFunctionPointerType(*)(const char*))dlsym(RTLD_NEXT, "eglGetProcAddress");
    if (!procname) return NULL;
    if (strcmp(procname, "glViewport")==0) return (__eglMustCastToProperFunctionPointerType)glViewport;
    if (strcmp(procname, "glScissor")==0) return (__eglMustCastToProperFunctionPointerType)glScissor;
    if (strcmp(procname, "glEnable")==0) return (__eglMustCastToProperFunctionPointerType)glEnable;
    if (real) return real(procname);
    return NULL;
}
