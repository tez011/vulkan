#include "fs.h"
#include "config.h"
#include <assert.h>
#include <physfs.h>
#include <spdlog/spdlog.h>

using std::ios_base;

namespace fs {

class fstreambuf : public std::streambuf {
private:
    int_type underflow()
    {
        if (PHYSFS_eof(m_file))
            return traits_type::eof();

        size_t consumed = PHYSFS_readBytes(m_file, m_buffer, s_buffer_size);
        if (consumed < 1)
            return traits_type::eof();

        setg(m_buffer, m_buffer, m_buffer + consumed);
        return static_cast<unsigned char>(*gptr());
    }

    pos_type seekoff(off_type pos, ios_base::seekdir dir, ios_base::openmode mode)
    {
        switch (dir) {
        case std::ios_base::beg:
            return seekpos(pos, mode);
        case std::ios_base::cur:
            return seekpos(PHYSFS_tell(m_file) + pos - (egptr() - gptr()), mode);
        case std::ios_base::end:
            return seekpos(PHYSFS_fileLength(m_file) + pos, mode);
        default:
            abort();
        }
    }

    pos_type seekpos(pos_type pos, ios_base::openmode mode)
    {
        PHYSFS_seek(m_file, pos);

        if (mode & std::ios_base::in)
            setg(egptr(), egptr(), egptr());
        if (mode & std::ios_base::out)
            setp(m_buffer, m_buffer);

        return PHYSFS_tell(m_file);
    }

    int_type overflow(int_type c = traits_type::eof())
    {
        if (pptr() == pbase() && c == traits_type::eof())
            return 0;
        if (PHYSFS_writeBytes(m_file, pbase(), static_cast<PHYSFS_uint32>(pptr() - pbase())) < 1)
            return traits_type::eof();
        if (c != traits_type::eof()) {
            if (PHYSFS_writeBytes(m_file, &c, 1) < 1)
                return traits_type::eof();
        }
        return 0;
    }

    int sync()
    {
        return overflow();
    }

    char* m_buffer;
    static constexpr size_t s_buffer_size = 4096;

protected:
    PHYSFS_File* m_file;

public:
    fstreambuf(PHYSFS_File* file)
        : m_file(file)
    {
        m_buffer = new char[4096];
        char* end = m_buffer + s_buffer_size;
        setg(end, end, end);
        setp(m_buffer, end);
    }
    fstreambuf(const fstreambuf&) = delete;
    ~fstreambuf()
    {
        sync();
        delete[] m_buffer;
    }
};

bool file::exists() const
{
    return PHYSFS_exists(m_path.c_str());
}

bool file::is_directory() const
{
    PHYSFS_Stat stat;
    if (PHYSFS_stat(m_path.c_str(), &stat) == 0) {
        spdlog::critical("PHYSFS_stat: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        abort();
    }
    return stat.filetype == PHYSFS_FILETYPE_DIRECTORY;
}

std::string_view file::basename() const
{
    size_t last_sep = m_path.find_last_of('/');
    if (last_sep == std::string::npos)
        return std::string_view(m_path);
    else
        return std::string_view(m_path).substr(last_sep + 1);
}

std::string_view file::extension(bool all) const
{
    std::string_view filename = basename();
    size_t ext_start = all ? filename.find_first_of('.') : filename.find_last_of('.');
    if (ext_start == std::string_view::npos)
        return std::string_view { "" };
    else
        return filename.substr(ext_start + 1);
}

void file::mkdir() const
{
    if (PHYSFS_mkdir(m_path.c_str()) == 0) {
        spdlog::critical("PHYSFS_mkdir: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        abort();
    }
}

file file::parent() const
{
    size_t end = m_path.find_last_of('/');
    if (end == 0)
        return file("/");
    else
        return file(m_path.substr(0, end));
}

file file::relative(std::string_view rel_path) const
{
    std::string p;
    if (is_directory()) {
        p = m_path;
    } else {
        size_t end = m_path.find_last_of('/');
        if (end == 0)
            p = "/";
        else
            p = m_path.substr(0, end);
    }
    p.reserve(p.length() + rel_path.length());

    std::string_view i = rel_path;
    while (true) {
        size_t n = i.find('/');
        if (i.substr(0, 3) == "../") {
            size_t end = p.find_last_of('/');
            if (end == 0)
                p = "/";
            else
                p = p.substr(0, end);
        } else if (i.substr(0, 2) != "./") {
            p += "/";
            p += i.substr(0, n);
        }
        if (n == std::string_view::npos)
            break;
        else
            i = i.substr(n + 1);
    }

    return file(p);
}

std::vector<file> file::children() const
{
    if (is_directory() == false)
        return {};

    char **list = PHYSFS_enumerateFiles(m_path.c_str()), **i;
    std::vector<file> out;
    for (i = list; *i; i++)
        out.emplace_back(*i);
    PHYSFS_freeList(list);
    return out;
}

base_stream::base_stream(void* handle)
    : m_handle(handle)
{
    assert(m_handle != nullptr);
}

base_stream::~base_stream()
{
    PHYSFS_close(reinterpret_cast<PHYSFS_File*>(m_handle));
}

size_t base_stream::length() const
{
    return PHYSFS_fileLength(reinterpret_cast<PHYSFS_File*>(m_handle));
}

static void* physfs_open(const std::string& filename, char mode)
{
    PHYSFS_File* f = nullptr;
    switch (mode) {
    case 'r':
        f = PHYSFS_openRead(filename.c_str());
        break;
    case 'w':
        f = PHYSFS_openWrite(filename.c_str());
        break;
    case 'a':
        f = PHYSFS_openAppend(filename.c_str());
        break;
    }
    if (f == nullptr) {
        spdlog::critical("physfs_open: failed to open {}: {}", filename, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        abort();
    }
    return reinterpret_cast<void*>(f);
}

istream::istream(const std::string& filename)
    : base_stream(physfs_open(filename, 'r'))
    , std::istream(new fstreambuf(reinterpret_cast<PHYSFS_File*>(m_handle)))
{
}
istream::~istream()
{
    delete rdbuf();
}

ostream::ostream(const std::string& filename, char mode)
    : base_stream(physfs_open(filename, mode))
    , std::ostream(new fstreambuf(reinterpret_cast<PHYSFS_File*>(m_handle)))
{
}
ostream::~ostream()
{
    delete rdbuf();
}

static bool s_initted = false;
void init(const char* argv0)
{
    if (s_initted)
        return;

    if (PHYSFS_init(argv0) == 0) {
        spdlog::critical("PHYSFS_init: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        abort();
    }

#ifdef NDEBUG
#else // NDEBUG
    if (PHYSFS_mount(SOURCE_ROOT "/resources", "/rs", 1) == 0) {
        spdlog::critical("PHYSFS_init: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        abort();
    }
#endif // NDEBUG

    atexit([]() { PHYSFS_deinit(); });
    s_initted = true;
}

}
