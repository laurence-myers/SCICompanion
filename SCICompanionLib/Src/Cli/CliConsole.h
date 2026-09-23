#pragma once

// Where scic writes (plan section 7): a result goes to stdout (the list
// table, the source of --stdout); the progress, the diagnostics, the errors
// and the summary go to stderr. A test gives a console that keeps the text.

#include <fstream>
#include <memory>
#include <string>

namespace cli
{
    class ICliConsole
    {
    public:
        virtual ~ICliConsole() = default;
        // A result, for stdout.
        virtual void Out(const std::string &text) = 0;
        // Everything else, for stderr.
        virtual void Err(const std::string &text) = 0;
    };

    // The stdout and stderr of the process.
    class StdConsole : public ICliConsole
    {
    public:
        void Out(const std::string &text) override;
        void Err(const std::string &text) override;
    };

    // Keeps the text, for a test.
    class StringConsole : public ICliConsole
    {
    public:
        void Out(const std::string &text) override { out += text; }
        void Err(const std::string &text) override { err += text; }

        std::string out;
        std::string err;
    };

    // --log: passes everything on, and also writes it to a file.
    class LogFileConsole : public ICliConsole
    {
    public:
        LogFileConsole(ICliConsole &inner, const std::string &path);
        bool IsOpen() const { return _file.is_open(); }
        void Out(const std::string &text) override;
        void Err(const std::string &text) override;

    private:
        ICliConsole &_inner;
        std::ofstream _file;
    };
}
