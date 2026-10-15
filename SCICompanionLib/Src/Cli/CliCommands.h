#pragma once

// The commands of scic (plan section 4), for Cli.cpp: each command gets an
// open session and its options, and returns its exit code.

#include "CliConsole.h"
#include "ExitCodes.h"
#include "Result.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class GameSession;

namespace cli
{
    // The options of every command (plan section 4.1).
    struct CommonOptions
    {
        bool quiet = false;
        bool verbose = false;
        std::string logFile;
        std::string dataFolder;
        bool dryRun = false;
    };

    enum class DiagnosticLevel
    {
        Error,
        Warning,
        // A message with a script and a line (for example "An iteration
        // variable can not be indexed."): shown unless --quiet, as the GUI
        // shows it.
        Info,
        // A message with no line: with --verbose only.
        Message,
    };

    // What a command writes: a result to stdout; an error, a warning, a
    // message and a detail to stderr, as the verbosity allows. The log file
    // (--log) gets every message (plan sections 4.1 and 7). One lock covers
    // both: codecs log from worker threads.
    class CliOutput
    {
    public:
        CliOutput(ICliConsole &console, const CommonOptions &options, LogFile *log = nullptr) : _console(console), _options(options), _log(log) {}
        CliOutput(const CliOutput &) = delete;
        CliOutput &operator=(const CliOutput &) = delete;

        // To stdout, always.
        void Result(const std::string &text) { _Write(text, true, true); }
        // To stderr, always.
        void Error(const std::string &text) { _Write("scic: error: " + text + "\n", true, false); }
        // To stderr, unless --quiet.
        void Warning(const std::string &text)
        {
            _warnings++;
            _Write("scic: warning: " + text + "\n", !_options.quiet, false);
        }
        void Message(const std::string &text) { _Write(text + "\n", !_options.quiet, false); }
        // To stderr, with --verbose only.
        void Detail(const std::string &text) { _Write(text + "\n", _options.verbose && !_options.quiet, false); }
        // Help and lists of commands, to stdout.
        void Help(const std::string &text) { _Write(text, true, true); }
        // Help after a usage error, to stderr, always.
        void HelpAfterError(const std::string &text) { _Write(text, true, false); }
        // A debug dump that an option asked for, to stderr, always (also with
        // --quiet), with no "scic:" prefix: it is not a warning.
        void Dump(const std::string &text) { _Write(text + "\n", true, false); }
        // A compiler diagnostic in the MSBuild format (plan section 4.5), to
        // stderr: an error always, a warning or an info line unless --quiet,
        // a message with --verbose only.
        void Diagnostic(DiagnosticLevel level, const std::string &text)
        {
            bool show = (level == DiagnosticLevel::Error) || (((level == DiagnosticLevel::Warning) || (level == DiagnosticLevel::Info)) && !_options.quiet) ||
                ((level == DiagnosticLevel::Message) && _options.verbose && !_options.quiet);
            if (level == DiagnosticLevel::Warning)
            {
                _warnings++;
            }
            _Write(text + "\n", show, false);
        }
        // The warnings so far (Warning and a warning diagnostic), also those
        // that --quiet hides: a summary counts them.
        size_t Warnings() const { return _warnings.load(); }

    private:
        void _Write(const std::string &text, bool toConsole, bool toStdout)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (toConsole)
            {
                if (toStdout)
                {
                    _console.Out(text);
                }
                else
                {
                    _console.Err(text);
                }
            }
            if (_log)
            {
                _log->Write(text);
            }
        }

        ICliConsole &_console;
        const CommonOptions &_options;
        LogFile *_log;
        std::mutex _mutex;
        std::atomic<size_t> _warnings{ 0 };
    };

    // scic script list (plan section 4.3).
    struct ScriptListOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        std::string format = "text";
        bool derived = false;
    };

    // Prints the scripts. Success, or PartialFailure when a compiled script
    // that it read cannot be read. Fails for a bad selector (Usage), and
    // after Ctrl+C (Cancelled).
    sci::Result<ExitCode> RunScriptList(GameSession &session, const ScriptListOptions &options, CliOutput &output);

    // scic script decompile (plan section 4.4).
    struct ScriptDecompileOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        bool all = false;
        std::string gameIni = "update";     // update, create or none
        bool resetNames = false;
        bool updateStale = false;
        bool toStdout = false;
        bool textTuples = false;
        bool asmOnly = false;
        std::string badBranch = "fix";     // fix or asm
        bool debugControlFlow = false;
        bool debugInstructions = false;
        std::string debugFilter;
        // The file of the function report; empty: no report.
        std::string functionReport;
    };

    // The first line of a function report: the names of its columns.
    extern const char *const FunctionReportHeader;

    // True when --function-report may write over the file: there is no such
    // file, the file is empty, or it is a function report (its first line is
    // FunctionReportHeader).
    bool MayOverwriteFunctionReport(const std::string &path);

    // The function report can be written: its folder exists, and the file,
    // when it exists, opens for writing. The file does not change.
    sci::Status CheckFunctionReportFile(const std::string &path);

    // Writes a function report with no functions (its first line only).
    sci::Status WriteEmptyFunctionReport(const std::string &path);

    // Decompiles the scripts (RunDecompile) and prints the report: the
    // messages of the decompiler as they come, then the summary. --stdout
    // prints the source and writes nothing; --dry-run writes nothing and
    // lists the files that a run would write, with the stale scripts
    // (DecompileRunOptions::dryRun). The exit code of the report (plan
    // section 8); an error of the decompiler in a script that it wrote
    // (a function whose code it cannot find) is 6. With --function-report,
    // the report goes to its file at the end, also after Ctrl+C or a
    // dry run: a line for each function that the run decompiled (the last
    // decompile of a script counts), in the order of the scripts and of
    // the offsets; a report that cannot be written is 9. Fails when the run
    // cannot start: a bad selector, or --stdout with more than one script
    // (Usage).
    sci::Result<ExitCode> RunScriptDecompile(GameSession &session, const ScriptDecompileOptions &options, const CommonOptions &common, CliOutput &output);

    // scic script sco (plan section 4.6).
    struct ScriptScoOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        bool all = false;
    };

    // Makes the .sco files (GenerateObjectFiles) and prints each script
    // that was skipped or failed, then the summary. With --all, a script
    // with a source file and no compiled script, or with a name in
    // game.ini and no source file, is skipped and listed. The exit code:
    // 6 when a script failed (also for a syntax error, plan section 4.6), 9
    // when a .sco could not be written (a dry run: when it could not be), 7
    // after Ctrl+C, 1 for a bug. Fails for a bad selector (Usage).
    sci::Result<ExitCode> RunScriptSco(GameSession &session, const ScriptScoOptions &options, const CommonOptions &common, CliOutput &output);

    // scic script compile (plan section 4.5).
    struct ScriptCompileOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        bool all = false;
        std::string to = "patch";   // patch or package
        bool toGiven = false;
        bool intoVolume = false;    // another spelling of --to package
        bool replacePatches = false;
        std::string outDir;
        bool raw = false;
        int passes = 5;             // with --all
        bool passesGiven = false;
        bool failFast = false;
        bool noWarnUnused = false;
    };

    // Compiles the scripts as one batch (CompileScripts) and prints the
    // report: the diagnostics of the last pass in the MSBuild format (plan
    // section 4.5), the progress with --verbose, then what went where and
    // the totals. --dry-run writes nothing and lists what a run would
    // write. The exit code of the report (plan section 8). Fails when the
    // batch cannot start (the code of ExitCodeForStartError: 1, 2, 3, 7 or 8).
    sci::Result<ExitCode> RunScriptCompile(GameSession &session, const ScriptCompileOptions &options, const CommonOptions &common, CliOutput &output);

    // scic pic list (plan section 4.7).
    struct PicListOptions
    {
        std::string gameFolder;
        // Numbers and ranges (100-199); empty: every pic.
        std::vector<std::string> pics;
        std::string format = "text";
    };

    // The first line of the tsv table of pic list: the names of its columns.
    extern const char *const PicListTsvHeader;

    // Prints a row for each pic: its format and size, its cels, the cel with
    // the largest area (the background), where the pic comes from, its size
    // in bytes and its CRC-32, and the palette entries that its pixels and
    // its palette have. A pic of a format with no image that scic reads
    // (ega, vga1) gets a row with no cels. It writes nothing. Success, or
    // PartialFailure when a pic cannot be read (its row has the error).
    // Fails for a selector that is not a number or a range, and for a pic
    // that the game does not have (Usage), and after Ctrl+C (Cancelled).
    sci::Result<ExitCode> RunPicList(GameSession &session, const PicListOptions &options, CliOutput &output);

    // scic pic export (plan section 4.7).
    struct PicExportOptions
    {
        std::string gameFolder;
        // Numbers and ranges (100-199), or all.
        std::vector<std::string> pics;
        bool all = false;
        std::string outDir;
        std::string cel = "background";     // background or all
    };

    // Writes, for each pic, its cels as 8-bit BMP files and a .toml file
    // (PicSidecar.h) into the output folder: <pic>.bmp is the background cel,
    // and with --cel all each other cel is <pic>.cel<index>.bmp. The pixels
    // of a file are the bytes of the cel, and its colour table is the palette
    // of the pic over palette 999 of the game. The files of a pic are all
    // made before the first is written. --dry-run writes nothing and lists
    // the files. A pic runs in its own exception boundary, so a pic that
    // fails does not stop the next one. With --all, a pic with no cel is
    // skipped and counted; a named pic with no cel fails. The exit code: 6
    // when a pic failed (also a pic of a format or with a cel that scic does
    // not export), 9 when a file could not be written, 7 after Ctrl+C, 1 for
    // a bug. Fails when the run cannot start: the output folder does not
    // exist (NotFound), or it is the game folder or a folder inside it
    // (Usage), or a selector is bad (Usage).
    sci::Result<ExitCode> RunPicExport(GameSession &session, const PicExportOptions &options, const CommonOptions &common, CliOutput &output);

    // scic pic import (plan section 4.7).
    struct PicImportOptions
    {
        std::string gameFolder;
        // One image: the number of the pic, the BMP file, and the cel.
        std::string pic;
        std::string image;
        int cel = 0;
        // False: the image is the background cel.
        bool celGiven = false;
        // Or a folder with the files of scic pic export: each .toml file,
        // and the BMP files that it names.
        std::string inDir;
        std::string palette = "keep";       // keep or replace
        // "A-B": the palette entries that get the colours of the image.
        std::string paletteRange;
        bool allowUnusedIndices = false;
        std::string outDir;
    };

    // The entries of a --palette-range text ("80-235": two numbers of 0 to
    // 255, the first not more than the second). False for another text.
    bool ParsePaletteRange(const std::string &text, int &first, int &last);

    // Puts the pixels of 8-bit BMP files into cels of pics, and writes each
    // pic as a patch file into the output folder (never into the game). An
    // SCI2 pic changes in place: only the pixels of the cels change, and with
    // --palette replace the colours of the palette range, which come from
    // the colour table of the image. With --in-dir, each .toml file of the
    // folder gives a pic and the files of its cels; a CRC-32 in it that is
    // not that of the pic in the game is a warning.
    //
    // The checks of a pic, all before its file is written: each image is
    // 8-bit and not compressed, and has the size of its cel; a pixel has the
    // skip colour of the cel where the cel has it, and nowhere else; each
    // other pixel is an entry that the pic or palette 999 marks as used (the
    // error names the entries; --allow-unused-indices makes it a warning);
    // the palette range is inside the entries that the pic stores, and the
    // tables of the images of one pic have the same colours in it.
    //
    // A pic runs in its own exception boundary, so a pic that fails does not
    // stop the next one. --dry-run does all checks and writes nothing. The
    // exit code: 6 when a pic failed, 9 when a patch file could not be
    // written, 7 after Ctrl+C, 1 for a bug. Fails when the run cannot start:
    // the output folder or the input folder does not exist (NotFound), the
    // output folder is the game folder or a folder inside it, the pic is not
    // a number of a pic of the game, or the input folder has no .toml file
    // (Usage).
    sci::Result<ExitCode> RunPicImport(GameSession &session, const PicImportOptions &options, const CommonOptions &common, CliOutput &output);

    // scic view list (plan section 4.10).
    struct ViewListOptions
    {
        std::string gameFolder;
        // Numbers and ranges (100-199); empty: every view.
        std::vector<std::string> views;
        std::string format = "text";
    };

    // Prints a row for each view (MakeViewListRow): its format, its loops
    // and mirror loops, its stored cels and raw cels, its resolution byte,
    // where the view comes from, its size in bytes and its CRC-32, what its
    // palette stores, the count of the skip pixels, and the palette entries
    // that its palette marks and that its pixels have. A view of a format
    // with no image that scic reads (ega, vga1) gets a row with no cels. A
    // view runs in its own exception boundary. It writes nothing. Success,
    // or PartialFailure when a view cannot be read (its row has the error),
    // also a view whose reader repaired damaged data (the reader logs a
    // warning and puts data of its own in the place of the damaged data).
    // Fails for a selector that is not a number or a range, and for a view
    // that the game does not have (Usage), and after Ctrl+C (Cancelled).
    sci::Result<ExitCode> RunViewList(GameSession &session, const ViewListOptions &options, CliOutput &output);

    // scic view export (plan section 4.11).
    struct ViewExportOptions
    {
        std::string gameFolder;
        // Numbers and ranges (100-199), or all.
        std::vector<std::string> views;
        bool all = false;
        std::string outDir;
    };

    // Writes, for each view, a BMP file for each stored cel and a .toml file
    // (ViewSidecar.h) into the output folder (MakeViewExportFiles). The
    // pixels of a file are the palette entries of the cel, and its colour
    // table is the palette of the view over palette 999 of the game. The
    // files of a view are all made before the first is written. --dry-run
    // writes nothing and lists the files. A view runs in its own exception
    // boundary, so a view that fails does not stop the next one. With --all,
    // a view with no stored cel is skipped and counted; a named view with no
    // cel fails. The exit code: 6 when a view failed (also a view of a
    // format that scic does not read, and a view whose reader repaired
    // damaged data), 9 when a file could not be written, 7 after Ctrl+C, 1
    // for a bug. Fails when the run cannot start: the output folder does not
    // exist (NotFound), or it is the game folder or a folder inside it
    // (Usage), or a selector is bad (Usage).
    sci::Result<ExitCode> RunViewExport(GameSession &session, const ViewExportOptions &options, const CommonOptions &common, CliOutput &output);

    // scic view import (plan section 4.12).
    struct ViewImportOptions
    {
        std::string gameFolder;
        // One image: the view, the loop, the cel and the BMP file.
        std::string view;
        int loop = 0;
        int cel = 0;
        std::string image;
        // Or a folder with the files of scic view export.
        std::string inDir;
        std::string palette = "keep";       // keep or replace
        std::string paletteRange;
        bool allowUnusedIndices = false;
        std::string outDir;
    };

    // Puts the pixels of 8-bit BMP files into cels of views
    // (MakeViewImportData), and writes each view as a patch file into the
    // output folder (never into the game). The view changes in place
    // (ReplaceViewCelPixels). With --in-dir, each .toml file of the folder
    // gives a view and the files of its cels; a CRC-32 in it that is not
    // that of the view in the game is a warning. A view with no change (no
    // byte would be another byte) is a warning, and gets no patch file.
    //
    // After a run, the output folder has the patch file of a named view only
    // when this run changed the view: a view that fails, or that has no
    // change, loses the patch file that an earlier run left there (a
    // message says so), as in view palette.
    //
    // A view runs in its own exception boundary, so a view that fails does
    // not stop the next one. --dry-run does all checks, writes and removes
    // nothing. The exit code: 6 when a view failed, 9 when a patch file
    // could not be written or removed, 7 after Ctrl+C, 1 for a bug. Fails
    // when the run cannot start: the output folder or the input folder does
    // not exist (NotFound), the output folder is the game folder or a
    // folder inside it, the view is not a number of a view of the game, or
    // the input folder has no .toml file (Usage).
    sci::Result<ExitCode> RunViewImport(GameSession &session, const ViewImportOptions &options, const CommonOptions &common, CliOutput &output);

    // scic view palette (plan section 4.9).
    struct ViewPaletteOptions
    {
        std::string gameFolder;
        // Numbers and ranges (100-199).
        std::vector<std::string> views;
        // One of two actions. The first: an 8-bit BMP file whose colour table
        // has the new colours of the entries of the range.
        std::string colorsFrom;
        // The second: the entries of the range lose their "used" mark.
        bool clearUsed = false;
        // "A-B": the palette entries of the action.
        std::string paletteRange;
        std::string outDir;
    };

    // Changes the palette of each view in place, and writes the view as a
    // patch file into the output folder (never into the game). The source is
    // the most recent resource of the view. The new bytes are the bytes of
    // the view, with other values only in its palette:
    //  - colorsFrom: each entry of the range that the palette stores gets the
    //    colour of the colour table of the BMP file (ReplaceViewPaletteColors).
    //    An entry that the palette does not mark as used is a warning.
    //  - clearUsed: the entries of the range lose their "used" mark
    //    (ClearViewPaletteMarks): the flag bytes of 4-byte entries become 0,
    //    and a palette with one flag for all its entries stores fewer
    //    entries. A view with a pixel in an entry that loses its mark fails.
    // A view with no change (no byte of it would be another byte) is a
    // warning, and gets no patch file.
    //
    // After a run, the output folder has the patch file of a named view only
    // when this run changed the view: a view that fails, or that has no
    // change, loses the patch file that an earlier run left there (a
    // message says so), and a write that fails leaves no file.
    //
    // A view runs in its own exception boundary, so a view that fails does
    // not stop the next one: a view of a game version that is not SCI1.1 or
    // SCI2, a view with no palette, a view that a reader repaired (the
    // repair is the reason, also when the change fails), and a view that
    // fails a check of its action. --dry-run does all checks, writes and
    // removes nothing, and says what a run would write and remove. The exit
    // code: 6 when a view failed, 9 when a patch file could not be written
    // or removed, 7 after Ctrl+C, 1 for a bug. Fails when the
    // run cannot start: the output folder does not exist (NotFound), or it
    // is the game folder or a folder inside it (Usage), a selector or the
    // range is bad (Usage), the BMP file cannot be read (the errors of
    // ReadIndexedBmpFile), or its colour table does not have the entries of
    // the range (Usage).
    sci::Result<ExitCode> RunViewPalette(GameSession &session, const ViewPaletteOptions &options, const CommonOptions &common, CliOutput &output);

    // scic view add (plan section 4.13).
    struct ViewAddOptions
    {
        std::string gameFolder;
        // The number of the new view.
        std::string view;
        // A .toml file of the form of scic view export; the BMP files of its
        // cels are in its folder.
        std::string from;
        // The game can have the view.
        bool replace = false;
        bool allowUnusedIndices = false;
        std::string outDir;
    };

    // Makes a new view from a .toml file of the form of view export and the
    // BMP files of its cels (MakeAddedView, MakeNewViewBytes), and writes it
    // as a patch file into the output folder (never into the game). A run
    // has one view. It runs in an exception boundary: a view that fails is
    // an error line, and loses the patch file that an earlier run left in the
    // output folder (a message says so). --dry-run does all checks, writes
    // and removes nothing. The exit code: 6 when the view failed (also a game
    // version that is not SCI1.1 or SCI2), 9 when the patch file could not be
    // written or removed, 7 after Ctrl+C, 1 for a bug. Fails when the run
    // cannot start: the output folder or the .toml file does not exist
    // (NotFound), the output folder is the game folder or a folder inside
    // it, the view is not a number of 0 to 65535, or the game has the view
    // and there is no --replace (Usage).
    sci::Result<ExitCode> RunViewAdd(GameSession &session, const ViewAddOptions &options, const CommonOptions &common, CliOutput &output);

    // scic dev compare-structure (hidden): the structure of the functions of
    // a folder of decompiled scripts against another decompile.
    struct CompareStructureOptions
    {
        std::string expectedFolder;
        std::string actualFolder;
        std::string baselineFolder;     // empty: no baseline
        std::string outFile;            // empty: stdout
        std::vector<int> scripts;       // empty: every script
    };

    // The first line of the table of compare-structure.
    extern const char *const CompareStructureHeader;

    // Compares the .sc files of the actual folder with those of the expected
    // folder (CompareScriptFolders), and with those of the baseline folder
    // when it is given. Writes a table (tab-separated: script, key,
    // function, verdict, baseline verdict, change; the last two are empty
    // with no baseline) to the out file or stdout, then the count of each
    // verdict and change to stderr. A file that cannot be read or parsed is
    // a warning, and exit code 6. Fails when a folder does not exist
    // (NotFound).
    sci::Result<ExitCode> RunCompareStructure(const CompareStructureOptions &options, CliOutput &output);

    // scic dev compare-meaning (hidden): the meaning of each function of a
    // game against the same function of the script that a compile of the
    // decompiled text made.
    struct CompareMeaningOptions
    {
        std::string gameFolder;
        // The compiled scripts as scic script compile --out-dir --raw writes
        // them: script.<n>.bin and heap.<n>.bin.
        std::string recompiledFolder;
        std::string outFile;            // empty: stdout
        std::vector<int> scripts;       // empty: every script of the game
    };

    // The first line of the table of compare-meaning.
    extern const char *const CompareMeaningHeader;

    // Compares each function of each script of the game with the function of
    // the same key in the recompiled script (meaning::Compare). A script with
    // no file in the recompiled folder (its compile failed) gives each of its
    // functions UNCOMPARED, "not-recompiled". Writes a table (tab-separated:
    // script, key, function, offset, verdict, detail) to the out file or
    // stdout, then the count of each verdict to stderr. A script that cannot
    // be read is a warning, and exit code 6; a recompiled script that cannot
    // be read also gives each of its functions UNCOMPARED,
    // "recompiled-unreadable". Fails when the game does not open, or the
    // recompiled folder does not exist (NotFound).
    sci::Result<ExitCode> RunCompareMeaning(const CompareMeaningOptions &options, const std::string &dataFolder, CliOutput &output);

    // Where a folder is, against the game folder.
    enum class GameFolderPlace
    {
        Outside,
        GameFolder,     // the folder is the game folder
        Inside,         // the folder is a folder inside the game folder
    };

    // The folders are compared as files, so another name of a folder (another
    // case, a short name, a junction) is the same folder. A folder that does
    // not exist has the place of its first parent that exists.
    GameFolderPlace PlaceInGameFolder(const std::string &gameFolder, const std::string &folder);

    // scic dev palette-use (hidden): the palette entries of the views and of
    // the palette resources of a game.
    struct PaletteUseOptions
    {
        std::string gameFolder;
        std::string outFile;            // empty: stdout
        // Also the colour of each entry that a palette marks as used.
        bool colors = false;
    };

    // The first line of the table of palette-use.
    extern const char *const PaletteUseHeader;

    // Writes a table (tab-separated) to the out file or stdout, with a row
    // for each view and then for each palette resource of the game, from the
    // lowest number: kind (view or palette), number, source (package or
    // patch), loops, cels, palette_entries (the entries that the palette of
    // the resource marks as used, as runs: 0-63,80-235), pixel_entries (the
    // entries of the pixels of the cels of a view, without the transparent
    // colour of each cel), colors, error. A cell with no value has "-". With
    // colors, the cell has "entry=rrggbb" for each entry of palette_entries,
    // with a space between them. Then the count of the rows goes to stderr.
    //
    // The used entries of a palette resource, and of a view of SCI1.1 or
    // SCI2, come from the bytes of the palette (ReadUsedPaletteEntries);
    // those of another view come from the palette as the reader gives it.
    //
    // A resource runs in its own exception boundary. A resource that cannot
    // be read, and one whose damage a reader repaired with a warning in the
    // core log, has the error in its row and "-" in its other cells, and
    // the exit code is 6. The table is the result of the command, so
    // --dry-run does not stop the write of the out file. Fails when the game
    // does not open, when the out file is in the game folder (Usage), and
    // after Ctrl+C (Cancelled).
    sci::Result<ExitCode> RunPaletteUse(const PaletteUseOptions &options, const std::string &dataFolder, CliOutput &output);

    // The full path of a folder or a file, from GetFullPathName: a relative
    // path starts at the current folder, and the case of the path stays. A
    // separator at the end goes, except the one of a root (C:\, \\?\C:\). An
    // empty path stays empty; a path that GetFullPathName cannot take keeps
    // its text, less a separator at its end.
    std::string AbsolutePath(const std::string &path);
}
