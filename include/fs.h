#pragma once
#include <filesystem>
#include <iostream>
#include <optional>
#include <vector>

namespace fs {

void init(const char* argv0 = nullptr);

class file {
private:
    std::string m_path;

public:
    file(const std::string& path)
        : m_path(path)
    {
    }
    ~file()
    {
    }

    const std::string& path() const { return m_path; }
    bool exists() const;
    bool is_directory() const;

    void mkdir() const;
    file parent() const;
    file relative(const std::string& rel_path) const;
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
