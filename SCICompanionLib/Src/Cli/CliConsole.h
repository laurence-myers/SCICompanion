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

    // The text of the ANSI code page, in UTF-16.
    std::wstring AnsiToWide(const std::string &text);

    // The stdout and stderr of the process. A console gets the characters of
    // the text (its code page can be another one than the ANSI code page); a
    // file or a pipe gets the bytes of the ANSI code page.
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

    // True when the text starts with the header line of a log of scic, of
    // any version: "scic <version> log".
    bool IsLogHeader(const std::string &text);

    // --log: a file that gets every message, whatever the verbosity: -q and
    // the absence of -v do not filter the log. Its first line is the header
    // "scic <version> log", so that a later --log can write over it.
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
