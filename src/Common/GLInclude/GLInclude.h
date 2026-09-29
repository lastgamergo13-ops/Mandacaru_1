#pragma once

#include "glext.h"

#if BOOST_OS_WINDOWS > 0
#include "wglext.h"
#endif

#if ( BOOST_OS_LINUX || BOOST_OS_BSD ) > 0 && !defined(__ANDROID__)

// from Xlib
#define Bool int
#define Status int
#define True 1
#define False 0

// from system glx.h
typedef XID GLXContextID;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
typedef XID GLXPbuffer;
typedef XID GLXWindow;
typedef XID GLXFBConfigID;
typedef struct __GLXcontextRec *GLXContext;
typedef struct __GLXFBConfigRec *GLXFBConfig;

#define EGL_EGL_PROTOTYPES 0
#include "egl.h"
#undef EGL_EGL_PROTOTYPES
#include "glxext.h"

#undef Bool
#undef Status
#undef True
#undef False

#elif defined(__ANDROID__)

// Android has no GLX/Xlib - it uses EGL directly against the Mali/Adreno GLES driver
// (libEGL.so / libGLESv3.so), the same mechanism already used for Vulkan via libvulkan.so.
// We reuse this project's own bundled (prototype-less) egl.h, exactly like the desktop
// Linux/BSD path above - it already pulls in the real NDK <EGL/eglplatform.h> for the
// correct native types (EGLNativeWindowType = ANativeWindow*), and with
// EGL_EGL_PROTOTYPES=0 it only defines types/constants, not linkable function
// declarations, so there is no symbol clash with the dynamically loaded EGLFUNC pointers
// declared below via glFunctions.h.
#define EGL_EGL_PROTOTYPES 0
#include "egl.h"
#undef EGL_EGL_PROTOTYPES

#endif

namespace CemuGL
{
#define GLFUNC(__type, __name)	extern __type __name;
#define EGLFUNC(__type, __name) extern __type __name;
#include "glFunctions.h"
#undef GLFUNC
#undef EGLFUNC

// DSA-style helpers with fallback to legacy API if DSA is not supported

#define DSA_FORCE_DISABLE false // set to true to simulate DSA not being supported

static GLenum GetGLBindingFromTextureTarget(GLenum texTarget)
{
	switch(texTarget)
	{
	case GL_TEXTURE_1D: return GL_TEXTURE_BINDING_1D;
	case GL_TEXTURE_2D: return GL_TEXTURE_BINDING_2D;
	case GL_TEXTURE_3D: return GL_TEXTURE_BINDING_3D;
	case GL_TEXTURE_2D_ARRAY: return GL_TEXTURE_BINDING_2D_ARRAY;
	case GL_TEXTURE_CUBE_MAP: return GL_TEXTURE_BINDING_CUBE_MAP;
	case GL_TEXTURE_CUBE_MAP_ARRAY: return GL_TEXTURE_BINDING_CUBE_MAP_ARRAY;
	default:
		cemu_assert_unimplemented();
		return 0;
	}
}

static GLuint glCreateTextureWrapper(GLenum target)
{
	GLuint tex;
	if (glCreateTextures && !DSA_FORCE_DISABLE)
	{
		glCreateTextures(target, 1, &tex);
		return tex;
	}
	GLint originalTexture;
	glGetIntegerv(GetGLBindingFromTextureTarget(target), &originalTexture);
	glGenTextures(1, &tex);
	glBindTexture(target, tex);
	glBindTexture(target, originalTexture);
	return tex;
}

static void glTextureStorage1DWrapper(GLenum target, GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width)
{
	if (glTextureStorage1D && !DSA_FORCE_DISABLE)
	{
		glTextureStorage1D(texture, levels, internalformat, width);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glTexStorage1D(target, levels, internalformat, width);
	glBindTexture(target, originalTexture);
}

static void glTextureStorage2DWrapper(GLenum target, GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
{
	if (glTextureStorage2D && !DSA_FORCE_DISABLE)
	{
		glTextureStorage2D(texture, levels, internalformat, width, height);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glTexStorage2D(target, levels, internalformat, width, height);
	glBindTexture(target, originalTexture);
}

static void glTextureStorage3DWrapper(GLenum target, GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)
{
	if (glTextureStorage3D && !DSA_FORCE_DISABLE)
	{
		glTextureStorage3D(texture, levels, internalformat, width, height, depth);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glTexStorage3D(target, levels, internalformat, width, height, depth);
	glBindTexture(target, originalTexture);
}

static void glTextureSubImage1DWrapper(GLenum target, GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void* pixels)
{
	if (glTextureSubImage1D && !DSA_FORCE_DISABLE)
	{
		glTextureSubImage1D(texture, level, xoffset, width, format, type, pixels);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glTexSubImage1D(target, level, xoffset, width, format, type, pixels);
	glBindTexture(target, originalTexture);
}

static void glCompressedTextureSubImage1DWrapper(GLenum target, GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void* data)
{
	if (glCompressedTextureSubImage1D && !DSA_FORCE_DISABLE)
	{
		glCompressedTextureSubImage1D(texture, level, xoffset, width, format, imageSize, data);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glCompressedTexSubImage1D(target, level, xoffset, width, format, imageSize, data);
	glBindTexture(target, originalTexture);
}

static void glTextureSubImage2DWrapper(GLenum target, GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels)
{
	if (glTextureSubImage2D && !DSA_FORCE_DISABLE)
	{
		glTextureSubImage2D(texture, level, xoffset, yoffset, width, height, format, type, pixels);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
	glBindTexture(target, originalTexture);
}

static void glCompressedTextureSubImage2DWrapper(GLenum target, GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data)
{
	if (glCompressedTextureSubImage2D && !DSA_FORCE_DISABLE)
	{
		glCompressedTextureSubImage2D(texture, level, xoffset, yoffset, width, height, format, imageSize, data);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
	glBindTexture(target, originalTexture);
}

static void glTextureSubImage3DWrapper(GLenum target, GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels)
{
	if(glTextureSubImage3D && !DSA_FORCE_DISABLE)
	{
		glTextureSubImage3D(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
	glBindTexture(target, originalTexture);
}

static void glCompressedTextureSubImage3DWrapper(GLenum target, GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void* data)
{
	if(glCompressedTextureSubImage3D && !DSA_FORCE_DISABLE)
	{
		glCompressedTextureSubImage3D(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
	glBindTexture(target, originalTexture);
}

static void glTextureParameteriWrapper(GLenum target, GLuint texture, GLenum pname, GLint param)
{
	if (glTextureParameteri && !DSA_FORCE_DISABLE)
	{
		glTextureParameteri(texture, pname, param);
		return;
	}
	GLenum binding = GetGLBindingFromTextureTarget(target);
	GLint originalTexture;
	glGetIntegerv(binding, &originalTexture);
	glBindTexture(target, texture);
	glTexParameteri(target, pname, param);
	glBindTexture(target, originalTexture);
}

// glTextureView has no bind-based legacy equivalent (it never existed before DSA) and no
// core GLES version before 3.2. Some Android GPUs/drivers expose it as GL_OES_texture_view
// or GL_EXT_texture_view (same signature as the desktop core function), so we try those
// next. Returns false if none exist, in which case the caller must fall back to
// glCopyImageSubDataWrapper below instead of aliasing the original texture id directly
// (aliasing it under a second owning LatteTextureViewGL would double-delete the same GL
// texture object once both views are destroyed).
static bool glTextureViewWrapper(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers)
{
	if (glTextureView && !DSA_FORCE_DISABLE)
	{
		glTextureView(texture, target, origtexture, internalformat, minlevel, numlevels, minlayer, numlayers);
		return true;
	}
	if (glTextureViewOES)
	{
		glTextureViewOES(texture, target, origtexture, internalformat, minlevel, numlevels, minlayer, numlayers);
		return true;
	}
	if (glTextureViewEXT)
	{
		glTextureViewEXT(texture, target, origtexture, internalformat, minlevel, numlevels, minlayer, numlayers);
		return true;
	}
	return false; // caller must fall back to glCopyImageSubDataWrapper
}

// Last-resort fallback for when this device has none of glTextureView/OES/EXT at all (very
// old Mali Midgard parts on GLES 3.0/3.1 without either copy-image extension either would be
// the only devices actually hitting this). This is a one-time GPU-side copy into a
// freshly-allocated texture, not a real live view - if the source texture is written again
// later, InitAliasView must be called again to refresh it. That already happens naturally
// for the cases this project currently forces new texture ids for (format-overwrite views,
// aliasy sub-ranges - see LatteTextureViewGL's forceCreateNewTexId), so this is a safe
// approximation in practice, just not a byte-for-byte identical feature.
static bool glCopyImageSubDataWrapper(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
	GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ,
	GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth)
{
	if (glCopyImageSubData)
	{
		glCopyImageSubData(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight, srcDepth);
		return true;
	}
	if (glCopyImageSubDataOES)
	{
		glCopyImageSubDataOES(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight, srcDepth);
		return true;
	}
	if (glCopyImageSubDataEXT)
	{
		glCopyImageSubDataEXT(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight, srcDepth);
		return true;
	}
	return false;
}

static GLuint glCreateBufferWrapper()
{
	GLuint buf;
	if (glCreateBuffers && !DSA_FORCE_DISABLE)
	{
		glCreateBuffers(1, &buf);
		return buf;
	}
	GLint originalBuffer;
	glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &originalBuffer);
	glGenBuffers(1, &buf);
	glBindBuffer(GL_COPY_WRITE_BUFFER, buf);
	glBindBuffer(GL_COPY_WRITE_BUFFER, originalBuffer);
	return buf;
}

// glNamedBufferStorage / immutable storage (glBufferStorage) requires GL_EXT_buffer_storage
// on GLES and is not core anywhere in the GLES 3.x line. When neither the DSA nor the
// legacy immutable-storage entry point exists, we fall back to a regular *mutable* buffer
// via glBufferData - functionally equivalent for our purposes (this buffer is only ever
// written by us via glBufferSubData afterwards, never resized), just without the driver
// hint that the size is fixed forever.
static void glNamedBufferStorageWrapper(GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags)
{
	if (glNamedBufferStorage && !DSA_FORCE_DISABLE)
	{
		glNamedBufferStorage(buffer, size, data, flags);
		return;
	}
	GLint originalBuffer;
	glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &originalBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
	if (glBufferStorage)
		glBufferStorage(GL_COPY_WRITE_BUFFER, size, data, flags);
	else
		glBufferData(GL_COPY_WRITE_BUFFER, size, data, (flags & GL_DYNAMIC_STORAGE_BIT) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	glBindBuffer(GL_COPY_WRITE_BUFFER, originalBuffer);
}

static void glNamedBufferSubDataWrapper(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data)
{
	if (glNamedBufferSubData && !DSA_FORCE_DISABLE)
	{
		glNamedBufferSubData(buffer, offset, size, data);
		return;
	}
	GLint originalBuffer;
	glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &originalBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
	glBufferSubData(GL_COPY_WRITE_BUFFER, offset, size, data);
	glBindBuffer(GL_COPY_WRITE_BUFFER, originalBuffer);
}

}
using namespace CemuGL;
// this prevents Windows GL.h from being included:
#define __gl_h_
#define __GL_H__
