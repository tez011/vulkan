#pragma once
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace fs {

void init(const char* argv0 = nullptr);

class file {
private:
    const std::string m_path;

public:
    file(const std::string& path)
        : m_path(path)
    {
    }

    const std::string& path() const { return m_path; }
    bool exists() const;
    bool is_directory() const;
    std::string_view basename() const;
    std::string_view extension(bool all = false) const;

    void mkdir() const;
    file parent() const;
    file relative(std::string_view rel_path) const;
    std::vector<file> children() const;
};

class base_stream {
protected:
    void* m_handle;

    base_stream(void* handle);

public:
    virtual ~base_stream();
    size_t length() const;
};

class istream : public base_stream, public std::istream {
public:
    istream(const std::string& filename);
    istream(const file& f)
        : istream(f.path())
    {
    }
    virtual ~istream();

    fs::istream& seekg(std::streampos pos)
    {
        std::istream::seekg(pos);
        return *this;
    }
    fs::istream& seekg(std::streamoff off, ios_base::seekdir way)
    {
        std::istream::seekg(off, way);
        return *this;
    }
};

class ostream : public base_stream, public std::ostream {
public:
    ostream(const std::string& filename, char mode = 'w');
    ostream(const file& f)
        : ostream(f.path())
    {
    }
    virtual ~ostream();
};

}
