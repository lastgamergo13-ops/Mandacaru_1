#include <chrono>
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/RendererShaderGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"

#include "Cemu/FileCache/FileCache.h"

#include "config/ActiveSettings.h"
#include "config/LaunchSettings.h"

extern std::atomic_int g_compiled_shaders_total;
extern std::atomic_int g_compiled_shaders_async;

bool s_isLoadingShaders{false};

// Prints the source lines referenced by the driver error log (GLSL ES drivers are much less forgiving than desktop ones,
// the log alone usually isn't enough to tell what the offending generated code looks like)
static void LogShaderCompileFailureContext(const std::string& glslSource, const char* infoLog, uint64 baseHash, GLenum glShaderType)
{
	static std::atomic_int s_dumpCount{0};
	if (s_dumpCount.fetch_add(1) >= 24)
		return; // don't flood the log
	const char* typeName = glShaderType == GL_VERTEX_SHADER ? "vertex" : (glShaderType == GL_FRAGMENT_SHADER ? "fragment" : "geometry");
	cemuLog_log(LogType::Force, "[GLSL] {} shader {:016x} failed to compile", typeName, baseHash);
	// split source into lines
	std::vector<std::string_view> lines;
	{
		size_t pos = 0;
		while (pos <= glslSource.size())
		{
			size_t e = glslSource.find('\n', pos);
			if (e == std::string::npos)
				e = glslSource.size();
			size_t len = e - pos;
			if (len > 0 && glslSource[pos + len - 1] == '\r')
				len--;
			lines.emplace_back(glslSource.data() + pos, len);
			pos = e + 1;
		}
	}
	// find "0:<line>" occurrences in the log (Adreno/Mali/Mesa all use this form: "ERROR: 0:123: ...", "0:123(5): error: ...")
	std::vector<int> errorLines;
	for (const char* c = infoLog; *c; c++)
	{
		if (c[0] == '0' && c[1] == ':' && c[2] >= '0' && c[2] <= '9' && (c == infoLog || c[-1] == ' ' || c[-1] == '\n'))
		{
			int ln = atoi(c + 2);
			if (ln > 0 && std::find(errorLines.begin(), errorLines.end(), ln) == errorLines.end())
				errorLines.push_back(ln);
			if (errorLines.size() >= 4)
				break;
		}
	}
	for (int ln : errorLines)
	{
		for (int l = ln - 1; l <= ln + 1; l++)
		{
			if (l < 1 || l > (int)lines.size())
				continue;
			std::string_view lineStr = lines[l - 1];
			if (lineStr.size() > 300)
				lineStr = lineStr.substr(0, 300);
			cemuLog_log(LogType::Force, "[GLSL]  {}{:5}: {}", l == ln ? ">" : " ", l, lineStr);
		}
	}
	// V11.47: nas 2 primeiras falhas mostra tambem o preambulo (#define/#extension/uniform) para achar macros ausentes
	// (ex.: GET_FRAGCOORD sem definicao no Zelda BOTW).
	{
		static std::atomic_int s_preambleDumps{0};
		if (s_preambleDumps.fetch_add(1) < 2)
		{
			int shown = 0;
			for (size_t l = 0; l < lines.size() && l < 140 && shown < 70; l++)
			{
				std::string_view s = lines[l];
				if (s.rfind("#", 0) == 0 || s.rfind("uniform", 0) == 0 || s.find("GET_FRAGCOORD") != std::string_view::npos)
				{
					if (s.size() > 200)
						s = s.substr(0, 200);
					cemuLog_log(LogType::Force, "[GLSL-preambulo] {:4}: {}", l + 1, s);
					shown++;
				}
			}
		}
	}
	if (errorLines.empty())
	{
		// no line numbers in the log, show the beginning of the shader
		for (size_t l = 0; l < lines.size() && l < 12; l++)
			cemuLog_log(LogType::Force, "[GLSL]   {:5}: {}", l + 1, lines[l]);
	}
}

bool RendererShaderGL::loadBinary()
{
	if (!s_programBinaryCache)
		return false;
	if (m_isGameShader == false || m_isGfxPackShader)
		return false; // only non-custom 
	if (!glProgramBinary)
		return false; // OpenGL program binaries not supported

	cemu_assert_debug(m_baseHash != 0);
	uint64 h1, h2;
	GenerateShaderPrecompiledCacheFilename(m_type, m_baseHash, m_auxHash, h1, h2);
	std::vector<uint8> cacheFileData;
	if (!s_programBinaryCache->GetFile({h1, h2 }, cacheFileData))
		return false;
	if (cacheFileData.size() <= sizeof(uint32))
		return false;

	uint32 shaderBinFormat = *(uint32*)(cacheFileData.data());

	m_program = glCreateProgram();
	glProgramBinary(m_program, shaderBinFormat, cacheFileData.data()+4, cacheFileData.size()-4);

	int status = -1;
	glGetProgramiv(m_program, GL_LINK_STATUS, &status);
	if (status != GL_TRUE)
	{
		glDeleteProgram(m_program);
		m_program = 0;
		return false;
	}
	m_isCompiled = true;
	return true;
}

void RendererShaderGL::storeBinary()
{
	if (!s_programBinaryCache)
		return;
	if (!glGetProgramBinary)
		return;
	if (m_program == 0)
		return;
	if (!m_isGameShader || m_isGfxPackShader)
		return;

	GLint binaryLength = 0;
	glGetProgramiv(m_program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
	if (binaryLength > 0)
	{
		uint64 h1, h2;
		GenerateShaderPrecompiledCacheFilename(m_type, m_baseHash, m_auxHash, h1, h2);
		// build stored shader data (4 byte format + binary data)
		std::vector<uint8> storedBinary(binaryLength+sizeof(uint32), 0);
		GLenum binaryFormat = 0;
		glGetProgramBinary(m_program, binaryLength, NULL, &binaryFormat, storedBinary.data()+sizeof(uint32));		
		*(uint32*)(storedBinary.data() + 0) = binaryFormat;
		// store
		s_programBinaryCache->AddFileAsync({h1, h2 }, storedBinary.data(), storedBinary.size());
	}
}

RendererShaderGL::RendererShaderGL(ShaderType type, uint64 baseHash, uint64 auxHash, bool isGameShader, bool isGfxPackShader, const std::string& glslSource)
	: RendererShader(type, baseHash, auxHash, isGameShader, isGfxPackShader), m_glslSource(glslSource)
{
	GLenum glShaderType;
	switch (type)
	{
	case ShaderType::kVertex:
		glShaderType = GL_VERTEX_SHADER;
		break;
	case ShaderType::kFragment:
		glShaderType = GL_FRAGMENT_SHADER;
		break;
	case ShaderType::kGeometry:
		glShaderType = GL_GEOMETRY_SHADER;
		break;
	default:
		cemu_assert_debug(false);
	}

	if (s_isLoadingShaders)
	{
		if (loadBinary())
		{
			m_glslSource.clear();
			m_glslSource.shrink_to_fit();
			return;
		}
	}

	const auto compileT0 = std::chrono::steady_clock::now(); // V11.47: mede compilacao lenta do driver (Adreno travava ~12 s)
	m_shader_object = glCreateShader(glShaderType);

	const char *c_str = m_glslSource.c_str();
	const GLint size = (GLint)m_glslSource.size();
	glShaderSource(m_shader_object, 1, &c_str, &size);
	glCompileShader(m_shader_object);

	GLint log_length;
	glGetShaderiv(m_shader_object, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0)
	{
		char log[4096]{};
		GLsizei log_size;
		glGetShaderInfoLog(m_shader_object, std::min<uint32>(log_length, sizeof(log) - 1), &log_size, log);
		cemuLog_log(LogType::Force, "Error/Warning in shader:");
		cemuLog_log(LogType::Force, log);
		GLint compileOk = GL_TRUE;
		glGetShaderiv(m_shader_object, GL_COMPILE_STATUS, &compileOk);
		if (compileOk == GL_FALSE)
			LogShaderCompileFailureContext(m_glslSource, log, m_baseHash, glShaderType);
	}

	// set debug name
	if (LaunchSettings::NSightModeEnabled()) 
	{
		auto objNameStr = fmt::format("shader_{:016x}_{:016x}", m_baseHash, m_auxHash);
		glObjectLabel(GL_SHADER, m_shader_object, objNameStr.size(), objNameStr.c_str());
	}

	m_program = glCreateProgram();
	glProgramParameteri(m_program, GL_PROGRAM_SEPARABLE, GL_TRUE);
	glProgramParameteri(m_program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
	glAttachShader(m_program, m_shader_object);
	m_shader_attached = true;
	glLinkProgram(m_program);

	storeBinary();

	{
		const auto compileMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - compileT0).count();
		if (compileMs >= 500)
			cemuLog_log(LogType::Force, "[GLSL] compilacao LENTA: {} shader {:016x} aux={:016x} levou {} ms (fonte={} bytes)",
				glShaderType == GL_VERTEX_SHADER ? "vertex" : (glShaderType == GL_FRAGMENT_SHADER ? "fragment" : "geometry"),
				m_baseHash, m_auxHash, (long long)compileMs, (uint32)m_glslSource.size());
	}

	// count shader compilation
	if (!s_isLoadingShaders)
		++g_compiled_shaders_total;

	// we can throw away the GLSL code to conserve RAM
	m_glslSource.clear();
	m_glslSource.shrink_to_fit();
}

RendererShaderGL::~RendererShaderGL()
{
	CleanupShaderObj();

	if (m_program != 0)
		glDeleteProgram(m_program);
}

void RendererShaderGL::CleanupShaderObj()
{
	if (m_shader_object != 0 && m_shader_attached)
		glDetachShader(m_program, m_shader_object);

	if (m_shader_object != 0)
		glDeleteShader(m_shader_object);

	m_shader_object = 0;
}

void RendererShaderGL::PreponeCompilation(bool isRenderThread)
{
	// the logic for initiating compilation is currently in the constructor
	// here we only guarantee that it is finished before we return
	if (m_isCompiled)
		return;
	WaitForCompiled();
}

bool RendererShaderGL::IsCompiled()
{
	cemu_assert_debug(false);
	return true;
}

bool RendererShaderGL::WaitForCompiled()
{
	char infoLog[8 * 1024];
	if (m_isCompiled)
		return true;
	// check if compilation was successful
	GLint compileStatus = GL_FALSE;
	glGetShaderiv(m_shader_object, GL_COMPILE_STATUS, &compileStatus);
	if (compileStatus == 0)
	{
		uint32 infoLogLength, tempLength;
		glGetShaderiv(m_shader_object, GL_INFO_LOG_LENGTH, (GLint *)&infoLogLength);
		if (infoLogLength != 0)
		{
			tempLength = sizeof(infoLog) - 1;
			glGetShaderInfoLog(m_shader_object, std::min(infoLogLength, tempLength), (GLsizei*)&tempLength, (GLcharARB*)infoLog);
			infoLog[tempLength] = '\0';
			cemuLog_log(LogType::Force, "Compile error in shader. Log:");
			cemuLog_log(LogType::Force, infoLog);
		}
		m_isCompiled = true;
		CleanupShaderObj();
		return false;
	}
	// get shader binary
	GLint linkStatus = GL_FALSE;
	glGetProgramiv(m_program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus == 0)
	{
		uint32 infoLogLength, tempLength;
		glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, (GLint *)&infoLogLength);
		if (infoLogLength != 0)
		{
			tempLength = sizeof(infoLog) - 1;
			glGetProgramInfoLog(m_program, std::min(infoLogLength, tempLength), (GLsizei*)&tempLength, (GLcharARB*)infoLog);
			infoLog[tempLength] = '\0';
			cemuLog_log(LogType::Force, "Link error in shader. Log:");
			cemuLog_log(LogType::Force, infoLog);
		}
		m_isCompiled = true;
		CleanupShaderObj();
		return false;
	}

	m_isCompiled = true;
	CleanupShaderObj();
	return true;
}

sint32 RendererShaderGL::GetUniformLocation(const char* name)
{
	return glGetUniformLocation(m_program, name);
}

void RendererShaderGL::SetUniform1i(sint32 location, sint32 value)
{
	glProgramUniform1i(m_program, location, value);
}

void RendererShaderGL::SetUniform1f(sint32 location, float value)
{
	glProgramUniform1f(m_program, location, value);
}

void RendererShaderGL::SetUniform2fv(sint32 location, void* data, sint32 count)
{
	glProgramUniform2fv(m_program, location, count, (const GLfloat*)data);
}

void RendererShaderGL::SetUniform4iv(sint32 location, void* data, sint32 count)
{
	glProgramUniform4iv(m_program, location, count, (const GLint*)data);
}

void RendererShaderGL::ShaderCacheLoading_begin(uint64 cacheTitleId)
{
    cemu_assert_debug(!s_programBinaryCache); // should not be set, ShaderCacheLoading_Close() not called?
	// determine if cache is enabled
	bool usePrecompiled = false;
	switch (ActiveSettings::GetPrecompiledShadersOption())
	{
	case PrecompiledShaderOption::Auto:
		if (g_renderer->GetVendor() == GfxVendor::Nvidia)
			usePrecompiled = false;
		else
			usePrecompiled = true;
		break;
	case PrecompiledShaderOption::Enable:
		usePrecompiled = true;
		break;
	case PrecompiledShaderOption::Disable:
		usePrecompiled = false;
		break;
	default:
		UNREACHABLE;
	}

	cemuLog_log(LogType::Force, "Using precompiled shaders: {}", usePrecompiled ? "true" : "false");

	if (usePrecompiled)
	{
		// GLES: invalida binarios antigos (shaders ES mudaram na V11.19 e com clip control emulado o codigo do VS/GS e outro)
		const uint32 cacheMagic = GeneratePrecompiledCacheId() ^ (OpenGLRenderer::IsGLES() ? 0x11190000u : 0u) ^ (OpenGLRenderer::IsClipControlEmulated() ? 0xC11B0001u : 0u);
		const std::string cacheFilename = fmt::format("{:016x}_gl.bin", cacheTitleId);
        s_programBinaryCache = FileCache::Open(ActiveSettings::GetCachePath("shaderCache/precompiled/{}", cacheFilename), true, cacheMagic);
		if (s_programBinaryCache == nullptr)
			cemuLog_log(LogType::Force, "Unable to open OpenGL precompiled cache {}", cacheFilename);
	}
	s_isLoadingShaders = true;
}

void RendererShaderGL::ShaderCacheLoading_end()
{
	s_isLoadingShaders = false;
}

void RendererShaderGL::ShaderCacheLoading_Close()
{
    if(s_programBinaryCache)
    {
        delete s_programBinaryCache;
        s_programBinaryCache = nullptr;
    }
    g_compiled_shaders_total = 0;
    g_compiled_shaders_async = 0;
}

FileCache* RendererShaderGL::s_programBinaryCache{};
