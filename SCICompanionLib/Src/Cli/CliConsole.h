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

    // --log: a file that gets every message, whatever the verbosity (C1
    // review: before, -q and the absence of -v filtered the log too).
    class LogFile
    {
    public:
        explicit LogFile(const std::string &path);
        bool IsOpen() const { return _file.is_open(); }
        void Write(const std::string &text);

    private:
        std::ofstream _file;
    };
}
