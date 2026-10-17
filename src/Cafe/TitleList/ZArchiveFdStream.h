#pragma once
// V11.36: std::istream que le direto de um file descriptor com pread().
// Motivo: no Android 11 (Moto G8) reabrir o arquivo por /proc/self/fd/N da errno 13
// (EACCES). Lendo pelo fd que o ContentResolver ja entregou nao precisa reabrir nada.
#ifdef __ANDROID__
#include <istream>
#include <streambuf>
#include <memory>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstring>

class ZArchiveFdStreamBuf final : public std::streambuf
{
  public:
	// assume a posse do fd (fecha no destrutor)
	explicit ZArchiveFdStreamBuf(int fd) : m_fd(fd)
	{
		struct stat st{};
		if (fstat(fd, &st) == 0)
			m_size = (int64_t)st.st_size;
		m_buf.resize(kBufSize);
		setg(nullptr, nullptr, nullptr);
	}
	~ZArchiveFdStreamBuf() override
	{
		if (m_fd >= 0)
			close(m_fd);
	}

  protected:
	pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
	{
		int64_t target;
		if (dir == std::ios_base::beg)
			target = off;
		else if (dir == std::ios_base::cur)
			target = CurrentPos() + off;
		else
			target = m_size + off;
		if (target < 0)
			return pos_type(off_type(-1));
		m_pos = target;
		setg(nullptr, nullptr, nullptr);
		return pos_type(off_type(m_pos));
	}
	pos_type seekpos(pos_type pos, std::ios_base::openmode which) override
	{
		return seekoff(off_type(pos), std::ios_base::beg, which);
	}
	int_type underflow() override
	{
		if (gptr() && gptr() < egptr())
			return traits_type::to_int_type(*gptr());
		const int64_t pos = CurrentPos();
		const ssize_t n = pread(m_fd, m_buf.data(), m_buf.size(), (off_t)pos);
		if (n <= 0)
			return traits_type::eof();
		m_pos = pos;
		setg(m_buf.data(), m_buf.data(), m_buf.data() + n);
		return traits_type::to_int_type(*gptr());
	}
	std::streamsize xsgetn(char* s, std::streamsize count) override
	{
		std::streamsize done = 0;
		// primeiro o que ja esta no buffer
		if (gptr() && gptr() < egptr())
		{
			const std::streamsize avail = egptr() - gptr();
			const std::streamsize take = avail < count ? avail : count;
			memcpy(s, gptr(), (size_t)take);
			gbump((int)take);
			done += take;
		}
		if (done == count)
			return done;
		// resto direto do arquivo (blocos grandes nao passam pelo buffer)
		int64_t pos = CurrentPos();
		while (done < count)
		{
			const ssize_t n = pread(m_fd, s + done, (size_t)(count - done), (off_t)pos);
			if (n <= 0)
				break;
			done += n;
			pos += n;
		}
		m_pos = pos;
		setg(nullptr, nullptr, nullptr);
		return done;
	}

  private:
	static constexpr size_t kBufSize = 64 * 1024;
	int64_t CurrentPos() const
	{
		if (gptr())
			return m_pos + (gptr() - eback());
		return m_pos;
	}
	int m_fd;
	int64_t m_size = 0;
	int64_t m_pos = 0;
	std::vector<char> m_buf;
};

class ZArchiveFdIStream final : public std::istream
{
  public:
	explicit ZArchiveFdIStream(int fd) : std::istream(nullptr), m_buf(fd)
	{
		rdbuf(&m_buf);
	}

  private:
	ZArchiveFdStreamBuf m_buf;
};

inline std::unique_ptr<std::istream> MakeZArchiveFdIStream(int fd)
{
	return std::make_unique<ZArchiveFdIStream>(fd);
}
#endif
