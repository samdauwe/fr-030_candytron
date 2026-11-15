/* gl.h - the OpenGL ES 3.0 / OpenGL 3.3 core entry points of the renderer, behind one function table - single-header C99 library (stb style)

   The renderer talks to the driver only through a ct_gl that the host fills with its loader (SDL_GL_GetProcAddress natively; the
   functions linked into the module on the web). It owns no window or context. The list below is what the renderer (gles.h) and the player call; ct_gl_load fails with the name of the first function
   the driver does not offer.

   USAGE
     In exactly one translation unit:

         #define CT_GL_IMPLEMENTATION
         #include <candytron/render/gl.h>

   CONFIGURATION (before including the implementation)
     CT_GL_STATIC    make every API function static
*/
#ifndef CT_GL_H_INCLUDED
#define CT_GL_H_INCLUDED

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CT_GL_API
#ifdef CT_GL_STATIC
#define CT_GL_API static
#else
#define CT_GL_API extern
#endif
#endif

#define CT_GL_NO_ERROR         0u
#define CT_GL_DEPTH_BUFFER_BIT 0x00000100u
#define CT_GL_COLOR_BUFFER_BIT 0x00004000u
#define CT_GL_RENDERER         0x1F01u
#define CT_GL_VERSION          0x1F02u

/* the constants of the entry points below */
#define CT_GL_FALSE                  0u
#define CT_GL_TRUE                   1u
#define CT_GL_TRIANGLES              0x0004u
#define CT_GL_UNSIGNED_BYTE          0x1401u
#define CT_GL_UNSIGNED_SHORT         0x1403u
#define CT_GL_FLOAT                  0x1406u
#define CT_GL_NEVER                  0x0200u
#define CT_GL_LESS                   0x0201u
#define CT_GL_EQUAL                  0x0202u
#define CT_GL_LEQUAL                 0x0203u
#define CT_GL_GREATER                0x0204u
#define CT_GL_NOTEQUAL               0x0205u
#define CT_GL_GEQUAL                 0x0206u
#define CT_GL_ALWAYS                 0x0207u
#define CT_GL_ZERO                   0u
#define CT_GL_ONE                    1u
#define CT_GL_SRC_COLOR              0x0300u
#define CT_GL_ONE_MINUS_SRC_COLOR    0x0301u
#define CT_GL_SRC_ALPHA              0x0302u
#define CT_GL_ONE_MINUS_SRC_ALPHA    0x0303u
#define CT_GL_DST_ALPHA              0x0304u
#define CT_GL_ONE_MINUS_DST_ALPHA    0x0305u
#define CT_GL_DST_COLOR              0x0306u
#define CT_GL_ONE_MINUS_DST_COLOR    0x0307u
#define CT_GL_CW                     0x0900u
#define CT_GL_CCW                    0x0901u
#define CT_GL_CULL_FACE              0x0B44u
#define CT_GL_DEPTH_TEST             0x0B71u
#define CT_GL_BLEND                  0x0BE2u
#define CT_GL_BACK                   0x0405u
#define CT_GL_UNPACK_ALIGNMENT       0x0CF5u
#define CT_GL_PACK_ALIGNMENT         0x0D05u
#define CT_GL_TEXTURE_2D             0x0DE1u
#define CT_GL_RGBA                   0x1908u
#define CT_GL_RGBA8                  0x8058u
#define CT_GL_NEAREST                0x2600u
#define CT_GL_LINEAR                 0x2601u
#define CT_GL_NEAREST_MIPMAP_NEAREST 0x2700u
#define CT_GL_LINEAR_MIPMAP_NEAREST  0x2701u
#define CT_GL_NEAREST_MIPMAP_LINEAR  0x2702u
#define CT_GL_LINEAR_MIPMAP_LINEAR   0x2703u
#define CT_GL_TEXTURE_MAG_FILTER     0x2800u
#define CT_GL_TEXTURE_MIN_FILTER     0x2801u
#define CT_GL_TEXTURE_WRAP_S         0x2802u
#define CT_GL_TEXTURE_WRAP_T         0x2803u
#define CT_GL_REPEAT                 0x2901u
#define CT_GL_CLAMP_TO_EDGE          0x812Fu
#define CT_GL_TEXTURE0               0x84C0u
#define CT_GL_FUNC_ADD               0x8006u
#define CT_GL_FUNC_SUBTRACT          0x800Au
#define CT_GL_FUNC_REVERSE_SUBTRACT  0x800Bu
#define CT_GL_ARRAY_BUFFER           0x8892u
#define CT_GL_ELEMENT_ARRAY_BUFFER   0x8893u
#define CT_GL_STREAM_DRAW            0x88E0u
#define CT_GL_STATIC_DRAW            0x88E4u
#define CT_GL_FRAGMENT_SHADER        0x8B30u
#define CT_GL_VERTEX_SHADER          0x8B31u
#define CT_GL_COMPILE_STATUS         0x8B81u
#define CT_GL_LINK_STATUS            0x8B82u
#define CT_GL_FRAMEBUFFER            0x8D40u
#define CT_GL_RENDERBUFFER           0x8D41u
#define CT_GL_COLOR_ATTACHMENT0      0x8CE0u
#define CT_GL_DEPTH_ATTACHMENT       0x8D00u
#define CT_GL_DEPTH_COMPONENT24      0x81A6u
#define CT_GL_FRAMEBUFFER_COMPLETE   0x8CD5u

/* X(return type, name without the "gl" prefix, parameter list) */
#define CT_GL_FUNCTIONS(X)                                                                                                                                     \
  X(void, ActiveTexture, (unsigned texture))                                                                                                                   \
  X(void, AttachShader, (unsigned program, unsigned shader))                                                                                                   \
  X(void, BindBuffer, (unsigned target, unsigned buffer))                                                                                                      \
  X(void, BindFramebuffer, (unsigned target, unsigned framebuffer))                                                                                            \
  X(void, BindRenderbuffer, (unsigned target, unsigned renderbuffer))                                                                                          \
  X(void, BindSampler, (unsigned unit, unsigned sampler))                                                                                                      \
  X(void, BindTexture, (unsigned target, unsigned texture))                                                                                                    \
  X(void, BindVertexArray, (unsigned array))                                                                                                                   \
  X(void, BlendEquation, (unsigned mode))                                                                                                                      \
  X(void, BlendFunc, (unsigned sfactor, unsigned dfactor))                                                                                                     \
  X(void, BufferData, (unsigned target, ptrdiff_t size, const void *data, unsigned usage))                                                                     \
  X(unsigned, CheckFramebufferStatus, (unsigned target))                                                                                                       \
  X(void, Clear, (unsigned mask))                                                                                                                              \
  X(void, ClearColor, (float r, float g, float b, float a))                                                                                                    \
  X(void, ClearDepthf, (float depth))                                                                                                                          \
  X(void, ColorMask, (unsigned char r, unsigned char g, unsigned char b, unsigned char a))                                                                     \
  X(void, CompileShader, (unsigned shader))                                                                                                                    \
  X(unsigned, CreateProgram, (void))                                                                                                                           \
  X(unsigned, CreateShader, (unsigned type))                                                                                                                   \
  X(void, CullFace, (unsigned mode))                                                                                                                           \
  X(void, DeleteBuffers, (int n, const unsigned *buffers))                                                                                                     \
  X(void, DeleteFramebuffers, (int n, const unsigned *framebuffers))                                                                                           \
  X(void, DeleteProgram, (unsigned program))                                                                                                                   \
  X(void, DeleteRenderbuffers, (int n, const unsigned *renderbuffers))                                                                                         \
  X(void, DeleteSamplers, (int n, const unsigned *samplers))                                                                                                   \
  X(void, DeleteShader, (unsigned shader))                                                                                                                     \
  X(void, DeleteTextures, (int n, const unsigned *textures))                                                                                                   \
  X(void, DeleteVertexArrays, (int n, const unsigned *arrays))                                                                                                 \
  X(void, DepthFunc, (unsigned func))                                                                                                                          \
  X(void, DepthMask, (unsigned char flag))                                                                                                                     \
  X(void, Disable, (unsigned cap))                                                                                                                             \
  X(void, DrawArrays, (unsigned mode, int first, int count))                                                                                                   \
  X(void, DrawElements, (unsigned mode, int count, unsigned type, const void *indices))                                                                        \
  X(void, Enable, (unsigned cap))                                                                                                                              \
  X(void, EnableVertexAttribArray, (unsigned index))                                                                                                           \
  X(void, DisableVertexAttribArray, (unsigned index))                                                                                                          \
  X(void, FramebufferRenderbuffer, (unsigned target, unsigned attachment, unsigned renderbuffertarget, unsigned renderbuffer))                                 \
  X(void, FramebufferTexture2D, (unsigned target, unsigned attachment, unsigned textarget, unsigned texture, int level))                                       \
  X(void, FrontFace, (unsigned mode))                                                                                                                          \
  X(void, GenBuffers, (int n, unsigned *buffers))                                                                                                              \
  X(void, GenFramebuffers, (int n, unsigned *framebuffers))                                                                                                    \
  X(void, GenRenderbuffers, (int n, unsigned *renderbuffers))                                                                                                  \
  X(void, GenSamplers, (int n, unsigned *samplers))                                                                                                            \
  X(void, GenTextures, (int n, unsigned *textures))                                                                                                            \
  X(void, GenVertexArrays, (int n, unsigned *arrays))                                                                                                          \
  X(void, GenerateMipmap, (unsigned target))                                                                                                                   \
  X(unsigned, GetError, (void))                                                                                                                                \
  X(void, GetProgramInfoLog, (unsigned program, int bufsize, int *length, char *infolog))                                                                      \
  X(void, GetProgramiv, (unsigned program, unsigned pname, int *params))                                                                                       \
  X(void, GetShaderInfoLog, (unsigned shader, int bufsize, int *length, char *infolog))                                                                        \
  X(void, GetShaderiv, (unsigned shader, unsigned pname, int *params))                                                                                         \
  X(const unsigned char *, GetString, (unsigned name))                                                                                                         \
  X(int, GetUniformLocation, (unsigned program, const char *name))                                                                                             \
  X(void, LinkProgram, (unsigned program))                                                                                                                     \
  X(void, PixelStorei, (unsigned pname, int param))                                                                                                            \
  X(void, ReadPixels, (int x, int y, int width, int height, unsigned format, unsigned type, void *pixels))                                                     \
  X(void, RenderbufferStorage, (unsigned target, unsigned internalformat, int width, int height))                                                              \
  X(void, SamplerParameteri, (unsigned sampler, unsigned pname, int param))                                                                                    \
  X(void, ShaderSource, (unsigned shader, int count, const char *const *string, const int *length))                                                            \
  X(void, TexImage2D, (unsigned target, int level, int internalformat, int width, int height, int border, unsigned format, unsigned type, const void *pixels)) \
  X(void, TexParameteri, (unsigned target, unsigned pname, int param))                                                                                         \
  X(void, Uniform1f, (int location, float v0))                                                                                                                 \
  X(void, Uniform1i, (int location, int v0))                                                                                                                   \
  X(void, Uniform2i, (int location, int v0, int v1))                                                                                                           \
  X(void, Uniform3i, (int location, int v0, int v1, int v2))                                                                                                   \
  X(void, Uniform3f, (int location, float v0, float v1, float v2))                                                                                             \
  X(void, Uniform4i, (int location, int v0, int v1, int v2, int v3))                                                                                           \
  X(void, Uniform4fv, (int location, int count, const float *value))                                                                                           \
  X(void, UniformMatrix4fv, (int location, int count, unsigned char transpose, const float *value))                                                            \
  X(void, UseProgram, (unsigned program))                                                                                                                      \
  X(void, VertexAttribPointer, (unsigned index, int size, unsigned type, unsigned char normalized, int stride, const void *pointer))                           \
  X(void, Viewport, (int x, int y, int width, int height))

typedef struct ct_gl {
#define CT_GL_MEMBER(ret, name, args) ret(*name) args;
  CT_GL_FUNCTIONS(CT_GL_MEMBER)
#undef CT_GL_MEMBER
} ct_gl;

typedef void *(*ct_gl_loader_fn)(const char *name);

/* Fills `gl` through `loader`. Returns NULL, or the "gl..." name of the first function the loader does not know (gl is then unusable). */
CT_GL_API const char *ct_gl_load(ct_gl *gl, ct_gl_loader_fn loader);

#ifdef __cplusplus
}
#endif

#endif /* CT_GL_H_INCLUDED */

#ifdef CT_GL_IMPLEMENTATION
#ifndef CT_GL_IMPLEMENTATION_DONE
#define CT_GL_IMPLEMENTATION_DONE

#include <string.h>

CT_GL_API const char *ct_gl_load(ct_gl *gl, ct_gl_loader_fn loader)
{
  memset(gl, 0, sizeof *gl);
#define CT_GL_LOAD(ret, name, args)                                                                                    \
  {                                                                                                                    \
    void *p = loader("gl" #name);                                                                                      \
    if (!p) return "gl" #name;                                                                                         \
    memcpy(&gl->name, &p, sizeof p); /* function pointers and void * have the same size on every supported platform */ \
  }
  CT_GL_FUNCTIONS(CT_GL_LOAD)
#undef CT_GL_LOAD
  return NULL;
}

#endif /* CT_GL_IMPLEMENTATION_DONE */
#endif /* CT_GL_IMPLEMENTATION */
