#include "stdafx.h"
#include "Cli.h"
#include "CliCommands.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "CliVersion.h"
#include "ExitCodes.h"
#include "GameSession.h"
#include "CoreLog.h"
#include "format.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
// CLI11 (from vcpkg, BSD-3-Clause), the argument parser, with no
// warnings of its own. It calls std::numeric_limits<T>::max(), which the
// min and max macros of windows.h break, so they are off for its text.
#pragma warning(push, 0)
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <CLI/CLI.hpp>
#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma warning(pop)

namespace fs = std::filesystem;

namespace cli
{
    namespace
    {
        // The core log of the engine: an error, a warning, and a message
        // with --verbose. CliOutput locks: codecs log from worker threads.
        class ConsoleLogSink : public ILogSink
        {
        public:
            explicit ConsoleLogSink(CliOutput &output) : _output(output) {}
            ~ConsoleLogSink() { RemoveCoreLogSink(this); }

            void Write(LogLevel level, const std::string &text) override
            {
                switch (level)
                {
                case LogLevel::Info:
                    _output.Detail(text);
                    break;
                case LogLevel::Warning:
                    _output.Warning(text);
                    break;
                default:
                    // An error shows also with -q.
                    _output.Error(text);
                    break;
                }
            }

        private:
            CliOutput &_output;
        };

        std::string ExeFolder()
        {
            char path[MAX_PATH] = {};
            DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if ((length == 0) || (length >= MAX_PATH))
            {
                return std::string();
            }
            return fs::path(path).parent_path().string();
        }

        // Plan section 3.5: --data-dir, else SCIC_DATA_DIR, else the folder of
        // scic.exe.
        std::string DataFolderOf(const CommonOptions &common)
        {
            if (!common.dataFolder.empty())
            {
                return common.dataFolder;
            }
            // A value of any length, also one longer than MAX_PATH.
            DWORD length = GetEnvironmentVariableA("SCIC_DATA_DIR", nullptr, 0);
            if (length > 1)
            {
                std::string value(length, '\0');
                DWORD copied = GetEnvironmentVariableA("SCIC_DATA_DIR", &value[0], length);
                if ((copied > 0) && (copied < length))
                {
                    value.resize(copied);
                    return value;
                }
            }
            return ExeFolder();
        }

        // True when --log may write over the file: there is no such file, the
        // file is empty, or it is a log of scic (its first line is the header
        // of LogFile). So --log <game>\resource.map, or a .log file that a
        // game ships, is refused. A name that is not a file (a folder, NUL)
        // goes to the open.
        bool MayOverwriteLog(const std::string &path)
        {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec) || (fs::file_size(path, ec) == 0))
            {
                return true;
            }
            std::ifstream file(path, std::ios::binary);
            char start[64] = {};
            file.read(start, sizeof(start) - 1);
            return IsLogHeader(std::string(start, (size_t)file.gcount()));
        }

        const char *CommonFooter =
            "Options of every command: -q (--quiet), -v (--verbose), --log <file>,\n"
            "--data-dir <folder>, --dry-run. See \"scic help\".";

        // A reversed copy: CLI11 parses a vector from its end.
        std::vector<std::string> Reversed(const std::vector<std::string> &args)
        {
            return std::vector<std::string>(args.rbegin(), args.rend());
        }

        // The help of one command, with its full name in the usage line
        // (App::help() gives the help of the chosen subcommand instead).
        std::string HelpOf(const CLI::App *command, const std::string &fullName)
        {
            return command->get_formatter()->make_help(command, fullName, CLI::AppFormatMode::Normal);
        }

        // The command that the arguments chose (the deepest subcommand), and
        // its full name.
        const CLI::App *ChosenCommand(const CLI::App &app, std::string &fullName)
        {
            const CLI::App *current = &app;
            fullName = "scic";
            for (;;)
            {
                std::vector<const CLI::App *> chosen = current->get_subcommands([](const CLI::App *) { return true; });
                std::vector<const CLI::App *> parsed;
                for (const CLI::App *sub : chosen)
                {
                    if (sub->parsed())
                    {
                        parsed.push_back(sub);
                    }
                }
                if (parsed.empty())
                {
                    return current;
                }
                current = parsed[0];
                fullName += " " + current->get_name();
            }
        }
    }

    int RunCli(const std::vector<std::string> &args, ICliConsole &console)
    {
        CommonOptions common;
        bool version = false;
        ScriptListOptions listOptions;
        ScriptDecompileOptions decompileOptions;
        ScriptScoOptions scoOptions;
        ScriptCompileOptions compileOptions;
        PicListOptions picListOptions;
        PicExportOptions picExportOptions;
        PicImportOptions picImportOptions;
        ViewListOptions viewListOptions;
        ViewExportOptions viewExportOptions;
        ViewImportOptions viewImportOptions;
        ViewPaletteOptions viewPaletteOptions;
        ViewAddOptions viewAddOptions;
        ViewRemoveOptions viewRemoveOptions;

        CLI::App app("scic: the command line of SCI Companion. Commands: script list, script decompile, script sco, script compile, pic list, pic export, pic import, view list, view export, view import, view add, view remove, view palette. \"scic help <group> <command>\" shows a command.", "scic");
        // "/games/sq3" is a path, not an option.
        app.allow_windows_style_options(false);
        // One command: the words after it are its own (for example the topic of
        // help), not another command.
        app.require_subcommand(0, 1);
        app.add_flag("-q,--quiet", common.quiet, "Show errors only.");
        app.add_flag("-v,--verbose", common.verbose, "Show more detail.");
        CLI::Option *logOption = app.add_option("--log", common.logFile, "Also write all messages to a file: a new file, an empty file, or a log of scic.");
        CLI::Option *dataFolderOption = app.add_option("--data-dir", common.dataFolder, "The folder that holds include\\ and Decompiler\\. Default: SCIC_DATA_DIR, or the folder of scic.exe.");
        app.add_flag("--dry-run", common.dryRun, "Do the work in memory, and write nothing into the game or into an output folder (--log and --function-report are still written).");
        app.add_flag("--version", version, "Show the version.");

        CLI::App *help = app.add_subcommand("help", "Show the help of a group or of a command: scic help script list.");
        // The words after "help" are its topic, not commands to run.
        help->prefix_command();

        CLI::App *script = app.add_subcommand("script", "The script commands: list, decompile, sco, compile.");
        script->fallthrough();
        script->require_subcommand(0, 1);
        script->footer(CommonFooter);

        CLI::App *list = script->add_subcommand("list", "Show the number and the name of each script. It writes nothing.");
        list->fallthrough();
        list->footer(CommonFooter);
        list->add_option("game-folder", listOptions.gameFolder, "The game folder.")->required();
        list->add_option("scripts", listOptions.selectors, "Numbers, ranges (100-199) or names. Default: every script.");
        list->add_option("--format", listOptions.format, "text (default) or tsv.")->check(CLI::IsMember({ "text", "tsv" }));
        list->add_flag("--derived", listOptions.derived, "Add the derived name of each script.");

        CLI::App *decompile = script->add_subcommand("decompile", "Decompile scripts into src\\<name>.sc and src\\<name>.sco, as the Decompile dialog does.");
        decompile->fallthrough();
        decompile->footer(CommonFooter);
        decompile->add_option("game-folder", decompileOptions.gameFolder, "The game folder.")->required();
        decompile->add_option("scripts", decompileOptions.selectors, "Numbers, ranges (100-199) or names. Or give --all.");
        decompile->add_flag("--all", decompileOptions.all, "Every script of the game.");
        decompile->add_option("--game-ini", decompileOptions.gameIni, "update (default): write the names into game.ini when it exists; create: also create it; none: never write it.")
            ->check(CLI::IsMember({ "update", "create", "none" }));
        decompile->add_flag("--reset-names", decompileOptions.resetNames, "Give each script its derived name, also a script that has a name.");
        decompile->add_flag("--update-stale", decompileOptions.updateStale, "Also decompile the scripts that use a renamed global by its old name.");
        decompile->add_flag("--stdout", decompileOptions.toStdout, "One script: print its source, and write nothing.");
        decompile->add_flag("--text-tuples", decompileOptions.textTuples, "Replace text resource tuples with strings.");
        decompile->add_flag("--asm-only", decompileOptions.asmOnly, "Disassemble only.");
        decompile->add_option("--bad-branch", decompileOptions.badBranch, "fix (default): a bnt that Sierra's compiler gave a bad target goes to the next instruction, and a branch out of the function reads the code there, in text where it can be; asm: a function with such a branch is asm.")
            ->check(CLI::IsMember({ "fix", "asm" }));
        decompile->add_flag("--debug-control-flow", decompileOptions.debugControlFlow, "Show the control flow (decompiler debug output).");
        decompile->add_flag("--debug-instructions", decompileOptions.debugInstructions, "Show the use of the instructions (decompiler debug output).");
        decompile->add_option("--debug-filter", decompileOptions.debugFilter, "The debug output only for this function.");
        CLI::Option *functionReportOption = decompile->add_option("--function-report", decompileOptions.functionReport,
            "Also write a line for each function into this file (tab-separated): its script, class, name, offset and bytes, the output (scope, asm, corrupt, error, stale or outside), and the result of the scope engine (ok, or why it failed).");

        CLI::App *sco = script->add_subcommand("sco", "Make src\\<name>.sco from src\\<name>.sc and the compiled script, for source from another tool.");
        sco->fallthrough();
        sco->footer(CommonFooter);
        sco->add_option("game-folder", scoOptions.gameFolder, "The game folder.")->required();
        sco->add_option("scripts", scoOptions.selectors, "Numbers, ranges (100-199) or names. Or give --all.");
        sco->add_flag("--all", scoOptions.all, "Every script with a source file and a compiled script.");

        CLI::App *compile = script->add_subcommand("compile", "Compile scripts, as SCI Companion does, into patch files (default) or the package, as one batch.");
        compile->fallthrough();
        compile->footer(CommonFooter);
        compile->add_option("game-folder", compileOptions.gameFolder, "The game folder.")->required();
        compile->add_option("scripts", compileOptions.selectors, "Numbers, ranges (100-199), names or .sc files. Or give --all.");
        compile->add_flag("--all", compileOptions.all, "Every script that has a source file.");
        CLI::Option *toOption = compile->add_option("--to", compileOptions.to, "patch (default): patch files in the game folder; package: the resource package.")
            ->check(CLI::IsMember({ "patch", "package" }));
        compile->add_flag("--into-volume", compileOptions.intoVolume, "The same as --to package.");
        compile->add_flag("--replace-patches", compileOptions.replacePatches, "With --to package: move the patch files that would hide the new resources to replaced-patches\\<time>.");
        CLI::Option *outDirOption = compile->add_option("--out-dir", compileOptions.outDir, "Write the patch files into this folder; the game's resources do not change (its .sco files do).");
        compile->add_flag("--raw", compileOptions.raw, "With --out-dir: the plain resource data, as script.110.bin.");
        CLI::Option *passesOption = compile->add_option("--passes", compileOptions.passes, "With --all: the most passes (default 5). A pass that changes no .sco file is the last.")
            ->check(CLI::Range(1, 100));
        compile->add_flag("--fail-fast", compileOptions.failFast, "Stop after the first script that fails.");
        compile->add_flag("--no-warn-unused", compileOptions.noWarnUnused, "No warning for an instance that is not used.");

        CLI::App *pic = app.add_subcommand("pic", "The pic commands: list, export, import.");
        pic->fallthrough();
        pic->require_subcommand(0, 1);
        pic->footer(CommonFooter);

        CLI::App *picList = pic->add_subcommand("list", "Show the cels and the palette use of each pic. It writes nothing.");
        picList->fallthrough();
        picList->footer(CommonFooter);
        picList->add_option("game-folder", picListOptions.gameFolder, "The game folder.")->required();
        picList->add_option("--pic", picListOptions.pics, "Pic numbers or ranges (100-199). Default: every pic.");
        picList->add_option("--format", picListOptions.format, "text (default) or tsv.")->check(CLI::IsMember({ "text", "tsv" }));

        CLI::App *picExport = pic->add_subcommand("export", "Write the cels of VGA pics as 8-bit BMP files, with a .toml file for each pic. The pixels are the palette entries of the game.");
        picExport->fallthrough();
        picExport->footer(CommonFooter);
        picExport->add_option("game-folder", picExportOptions.gameFolder, "The game folder.")->required();
        picExport->add_option("--pic", picExportOptions.pics, "Pic numbers or ranges (100-199). Or give --all.");
        picExport->add_flag("--all", picExportOptions.all, "Every pic of the game.");
        CLI::Option *picExportOutDirOption = picExport->add_option("--out-dir", picExportOptions.outDir, "The folder for the files. It must exist, and it must not be the game folder or a folder inside it.")->required();
        picExport->add_option("--cel", picExportOptions.cel, "background (default): the cel with the largest area, as <pic>.bmp; all: also each other cel, as <pic>.cel<index>.bmp.")
            ->check(CLI::IsMember({ "background", "all" }));

        CLI::App *picImport = pic->add_subcommand("import", "Put the pixels of 8-bit BMP files into cels of VGA pics, and write each pic as a patch file into a folder. The game does not change.");
        picImport->fallthrough();
        picImport->footer(CommonFooter);
        picImport->add_option("game-folder", picImportOptions.gameFolder, "The game folder.")->required();
        CLI::Option *importPicOption = picImport->add_option("--pic", picImportOptions.pic, "The number of the pic of --image.");
        CLI::Option *importImageOption = picImport->add_option("--image", picImportOptions.image, "An 8-bit BMP file, not compressed, with the size of the cel.");
        CLI::Option *importCelOption = picImport->add_option("--cel", picImportOptions.cel, "The cel of --image. Default: the cel with the largest area.")
            ->check(CLI::Range(0, 255));
        CLI::Option *importInDirOption = picImport->add_option("--in-dir", picImportOptions.inDir, "A folder with the files of scic pic export: each .toml file gives a pic and the BMP files of its cels.");
        picImport->add_option("--palette", picImportOptions.palette, "keep (default): the palette of the pic does not change; replace: the entries of --palette-range get the colours of the colour table of the image.")
            ->check(CLI::IsMember({ "keep", "replace" }));
        CLI::Option *importRangeOption = picImport->add_option("--palette-range", picImportOptions.paletteRange, "With --palette replace: the palette entries that get new colours, as first-last (for example 80-235). The pic must store them.");
        picImport->add_flag("--allow-unused-indices", picImportOptions.allowUnusedIndices, "A pixel with a palette entry that the pic and palette 999 do not mark as used is a warning, not an error.");
        CLI::Option *importOutDirOption = picImport->add_option("--out-dir", picImportOptions.outDir, "The folder for the patch files. It must exist, and it must not be the game folder or a folder inside it.")->required();

        CLI::App *view = app.add_subcommand("view", "The view commands: list, export, import, add, remove, palette.");
        view->fallthrough();
        view->require_subcommand(0, 1);
        view->footer(CommonFooter);

        CLI::App *viewList = view->add_subcommand("list", "Show the loops, the cels and the palette use of each view. It writes nothing.");
        viewList->fallthrough();
        viewList->footer(CommonFooter);
        viewList->add_option("game-folder", viewListOptions.gameFolder, "The game folder.")->required();
        viewList->add_option("--view", viewListOptions.views, "View numbers or ranges (100-199). Default: every view.");
        viewList->add_option("--format", viewListOptions.format, "text (default) or tsv.")->check(CLI::IsMember({ "text", "tsv" }));

        CLI::App *viewExport = view->add_subcommand("export", "Write each stored cel of views of SCI1.1 and SCI2 as an 8-bit BMP file (<view>.l<loop>.c<cel>.bmp), with a .toml file for each view. The pixels are the palette entries of the game.");
        viewExport->fallthrough();
        viewExport->footer(CommonFooter);
        viewExport->add_option("game-folder", viewExportOptions.gameFolder, "The game folder.")->required();
        viewExport->add_option("--view", viewExportOptions.views, "View numbers or ranges (100-199). Or give --all.");
        viewExport->add_flag("--all", viewExportOptions.all, "Every view of the game.");
        CLI::Option *viewExportOutDirOption = viewExport->add_option("--out-dir", viewExportOptions.outDir, "The folder for the files. It must exist, and it must not be the game folder or a folder inside it.")->required();

        CLI::App *viewImport = view->add_subcommand("import", "Put the pixels of 8-bit BMP files into cels of views of SCI1.1 and SCI2, and write each view as a patch file into a folder. The view changes in place. The game does not change.");
        viewImport->fallthrough();
        viewImport->footer(CommonFooter);
        viewImport->add_option("game-folder", viewImportOptions.gameFolder, "The game folder.")->required();
        CLI::Option *importViewOption = viewImport->add_option("--view", viewImportOptions.view, "The number of the view of --image.");
        CLI::Option *importLoopOption = viewImport->add_option("--loop", viewImportOptions.loop, "The loop of --image (default 0).")->check(CLI::Range(0, 255));
        CLI::Option *importViewCelOption = viewImport->add_option("--cel", viewImportOptions.cel, "The cel of --image (default 0).")->check(CLI::Range(0, 255));
        CLI::Option *importViewImageOption = viewImport->add_option("--image", viewImportOptions.image, "An 8-bit BMP file, not compressed, with the size of the cel.");
        CLI::Option *importViewInDirOption = viewImport->add_option("--in-dir", viewImportOptions.inDir, "A folder with the files of scic view export: each .toml file gives a view and the BMP files of its cels.");
        viewImport->add_option("--palette", viewImportOptions.palette, "keep (default): the palette of the view does not change; replace: the entries of --palette-range that the palette of the view stores get the colours of the colour table of the images.")
            ->check(CLI::IsMember({ "keep", "replace" }));
        CLI::Option *importViewRangeOption = viewImport->add_option("--palette-range", viewImportOptions.paletteRange, "With --palette replace: the palette entries that get new colours, as first-last (for example 80-235).");
        viewImport->add_flag("--allow-unused-indices", viewImportOptions.allowUnusedIndices, "A pixel with a palette entry that the view and palette 999 do not mark as used is a warning, not an error.");
        viewImport->add_flag("--allow-skip-changes", viewImportOptions.allowSkipChanges, "A pixel can change to or from the skip colour of its cel (for example an outline around a sprite); the cel is encoded again. Without it, the pixels of the skip colour must stay where they are.");
        CLI::Option *importSizeChangesOption = viewImport->add_flag("--allow-size-changes", viewImportOptions.allowSizeChanges, "With --in-dir: an image can have another size than its cel. The cel gets the size of the image and the x and y of its [[cel]] in the .toml file, and the skip pixels of such a cel can change. Without it, an image must have the size of its cel.");
        CLI::Option *importViewOutDirOption = viewImport->add_option("--out-dir", viewImportOptions.outDir, "The folder for the patch files. It must exist, and it must not be the game folder or a folder inside it.")->required();

        CLI::App *viewAdd = view->add_subcommand("add", "Make a new view of SCI1.1 or SCI2 from a .toml file of the form of scic view export and the 8-bit BMP files of its cels, and write it as a patch file into a folder. The game does not change.");
        viewAdd->fallthrough();
        viewAdd->footer(CommonFooter);
        viewAdd->add_option("game-folder", viewAddOptions.gameFolder, "The game folder.")->required();
        viewAdd->add_option("--view", viewAddOptions.view, "The number of the new view (0 to 65535).")->required();
        CLI::Option *addFromOption = viewAdd->add_option("--from", viewAddOptions.from, "A .toml file of the form of scic view export: the loops, the cels, the palette and the BMP file of each cel (in the folder of the .toml file).")->required();
        viewAdd->add_flag("--replace", viewAddOptions.replace, "The game can have the view: the patch file then takes its place when it goes into the game.");
        viewAdd->add_flag("--allow-unused-indices", viewAddOptions.allowUnusedIndices, "A pixel with a palette entry that the new palette and palette 999 do not mark as used is a warning, not an error.");
        CLI::Option *addOutDirOption = viewAdd->add_option("--out-dir", viewAddOptions.outDir, "The folder for the patch file. It must exist, and it must not be the game folder or a folder inside it.")->required();

        CLI::App *viewRemove = view->add_subcommand("remove", "Remove views from the game: each copy (the patch files, and the entries in the package: the map and the volume are written again), or only the patch files. It changes the game. A script that uses a removed view fails in the game.");
        viewRemove->fallthrough();
        viewRemove->footer(CommonFooter);
        viewRemove->add_option("game-folder", viewRemoveOptions.gameFolder, "The game folder.")->required();
        viewRemove->add_option("--view", viewRemoveOptions.views, "View numbers or ranges (100-199).")->required();
        viewRemove->add_option("--from", viewRemoveOptions.from, "all (default): each copy of the view; patch: only its patch files, so that the game uses the copy in the package, or has no copy of the view.")
            ->check(CLI::IsMember({ "all", "patch" }));

        CLI::App *viewPalette = view->add_subcommand("palette", "Change the palette of views of SCI1.1 and SCI2 in place: new colours for a range of entries, or no \"used\" mark for them. Each view is written as a patch file into a folder. The game does not change.");
        viewPalette->fallthrough();
        viewPalette->footer(CommonFooter);
        viewPalette->add_option("game-folder", viewPaletteOptions.gameFolder, "The game folder.")->required();
        viewPalette->add_option("--view", viewPaletteOptions.views, "View numbers or ranges (100-199).")->required();
        CLI::Option *viewColorsFromOption = viewPalette->add_option("--colors-from", viewPaletteOptions.colorsFrom, "An 8-bit BMP file, not compressed: the entries of --palette-range that the palette of the view stores get the colours of its colour table. The \"used\" marks stay.");
        viewPalette->add_flag("--clear-used", viewPaletteOptions.clearUsed, "The entries of --palette-range lose their \"used\" mark, so the view does not give them to the screen. A palette with one flag for all its entries then stores fewer entries: the range must reach its last stored entry. A view with a pixel in such an entry fails.");
        viewPalette->add_option("--palette-range", viewPaletteOptions.paletteRange, "The palette entries of the action, as first-last (for example 80-235).")->required();
        CLI::Option *viewOutDirOption = viewPalette->add_option("--out-dir", viewPaletteOptions.outDir, "The folder for the patch files. It must exist, and it must not be the game folder or a folder inside it.")->required();

        // Tools for the development of scic. The empty group hides them from
        // the help of scic.
        CompareStructureOptions compareOptions;
        CLI::App *dev = app.add_subcommand("dev", "Tools for the development of scic.");
        dev->group("");
        dev->fallthrough();
        dev->require_subcommand(0, 1);
        CLI::App *compareStructure = dev->add_subcommand("compare-structure", "Compare the structure of each function of decompiled scripts with another decompile of the game.");
        compareStructure->fallthrough();
        compareStructure->add_option("expected-folder", compareOptions.expectedFolder, "The .sc files to compare with (for example the output of another decompiler).")->required();
        compareStructure->add_option("actual-folder", compareOptions.actualFolder, "The .sc files of scic.")->required();
        CLI::Option *baselineOption = compareStructure->add_option("--baseline", compareOptions.baselineFolder, "The .sc files of an earlier decompile: the table gets the change of each function.");
        CLI::Option *compareOutOption = compareStructure->add_option("--out", compareOptions.outFile, "Write the table into this file (default: stdout).");
        compareStructure->add_option("--scripts", compareOptions.scripts, "Compare only these scripts (numbers, separated by commas).")
            ->delimiter(',')->check(CLI::Range(0, 65535));
        CompareMeaningOptions meaningOptions;
        CLI::App *compareMeaning = dev->add_subcommand("compare-meaning", "Compare the meaning of each function of a game with the same function of the script compiled from the decompiled text.");
        compareMeaning->fallthrough();
        compareMeaning->add_option("game-folder", meaningOptions.gameFolder, "The game with the original scripts.")->required();
        compareMeaning->add_option("recompiled-folder", meaningOptions.recompiledFolder, "The scripts compiled from the decompiled text (scic script compile --out-dir <folder> --raw).")->required();
        CLI::Option *meaningOutOption = compareMeaning->add_option("--out", meaningOptions.outFile, "Write the table into this file (default: stdout).");
        compareMeaning->add_option("--scripts", meaningOptions.scripts, "Compare only these scripts (numbers, separated by commas).")
            ->delimiter(',')->check(CLI::Range(0, 65535));
        PaletteUseOptions paletteUseOptions;
        CLI::App *paletteUse = dev->add_subcommand("palette-use", "Show the palette entries of each view and of each palette resource of a game: those that its palette marks as used, and those of the pixels of a view.");
        paletteUse->fallthrough();
        paletteUse->add_option("game-folder", paletteUseOptions.gameFolder, "The game folder.")->required();
        CLI::Option *paletteUseOutOption = paletteUse->add_option("--out", paletteUseOptions.outFile, "Write the table into this file (default: stdout).");
        paletteUse->add_flag("--colors", paletteUseOptions.colors, "Also the colour of each entry that a palette marks as used.");

        CliOutput output(console, common);
        try
        {
            app.parse(Reversed(args));
        }
        catch (const CLI::CallForHelp &)
        {
            // The help of the command that was given.
            std::string fullName;
            const CLI::App *command = ChosenCommand(app, fullName);
            output.Help(HelpOf(command, fullName));
            return (int)ExitCode::Success;
        }
        catch (const CLI::ParseError &error)
        {
            // Not in the log: the log file is an option too.
            output.Error(error.what());
            output.HelpAfterError("Run \"scic help\" for the commands and their options.\n");
            return (int)ExitCode::Usage;
        }

        // An error of --log itself does not go into a log.
        if ((logOption->count() > 0) && common.logFile.empty())
        {
            output.Error("--log needs a file");
            return (int)ExitCode::Usage;
        }
        if (!common.logFile.empty() && !MayOverwriteLog(common.logFile))
        {
            output.Error("--log would overwrite " + common.logFile + ", which is not a log of scic (its first line is not \"scic <version> log\"); give a new file, an empty file, or a log of scic");
            return (int)ExitCode::Usage;
        }

        // The other usage errors that need no game (plan section 8). They are
        // found before the log opens, so that a log that cannot open does not
        // change their exit code; the log then gets them.
        compileOptions.passesGiven = (passesOption->count() > 0);
        compileOptions.toGiven = (toOption->count() > 0);
        picImportOptions.celGiven = (importCelOption->count() > 0);
        std::string helpAfterUsageError;
        CLI::App *helpTopic = &app;
        std::string helpTopicName = "scic";
        auto findUsageError = [&]() -> std::string
        {
            if ((dataFolderOption->count() > 0) && common.dataFolder.empty())
            {
                // An empty --data-dir is an error: the folder of scic.exe does
                // not take its place.
                return "--data-dir needs a folder";
            }
            if ((outDirOption->count() > 0) && compileOptions.outDir.empty())
            {
                // An empty --out-dir (for example an unset variable in a build
                // script) is an error, so that the compile does not write into
                // the game, as with no --out-dir.
                return "--out-dir needs a folder";
            }
            // Plan section 4.2: decompile, sco and compile take --all or one or
            // more scripts, not both and not neither.
            for (const auto &command : { std::make_pair(decompile, std::make_pair(decompileOptions.all, !decompileOptions.selectors.empty())),
                std::make_pair(sco, std::make_pair(scoOptions.all, !scoOptions.selectors.empty())),
                std::make_pair(compile, std::make_pair(compileOptions.all, !compileOptions.selectors.empty())) })
            {
                if (command.first->parsed() && (command.second.first == command.second.second))
                {
                    return command.second.first ? "give --all or scripts, not both" : "give --all, or one or more scripts";
                }
            }
            if (decompile->parsed() && decompileOptions.toStdout && decompileOptions.all)
            {
                return "--stdout takes one script, not --all";
            }
            if ((picExportOutDirOption->count() > 0) && picExportOptions.outDir.empty())
            {
                // An empty --out-dir is not the current folder.
                return "--out-dir needs a folder";
            }
            // Plan section 4.7: export takes --all or one or more pics, not
            // both and not neither.
            if (picExport->parsed() && (picExportOptions.all == !picExportOptions.pics.empty()))
            {
                return picExportOptions.all ? "give --all or --pic, not both" : "give --all, or --pic with one or more pics";
            }
            if (picImport->parsed())
            {
                // Plan section 4.7: import takes one image (--pic and --image,
                // with --cel or not) or a folder (--in-dir), not both and not
                // neither.
                const bool oneImage = (importPicOption->count() > 0) || (importImageOption->count() > 0) || (importCelOption->count() > 0);
                const bool folder = (importInDirOption->count() > 0);
                if ((importOutDirOption->count() > 0) && picImportOptions.outDir.empty())
                {
                    return "--out-dir needs a folder";
                }
                if (folder && picImportOptions.inDir.empty())
                {
                    return "--in-dir needs a folder";
                }
                if (oneImage == folder)
                {
                    return folder ? "give --in-dir, or --pic with --image, not both" : "give --in-dir, or --pic with --image";
                }
                if (oneImage && (picImportOptions.pic.empty() || picImportOptions.image.empty()))
                {
                    return "an image needs --pic and --image";
                }
                const bool replace = (picImportOptions.palette == "replace");
                const bool range = (importRangeOption->count() > 0);
                if (range && !replace)
                {
                    return "--palette-range needs --palette replace";
                }
                if (replace && !range)
                {
                    return "--palette replace needs --palette-range";
                }
                int first = 0;
                int last = 0;
                if (range && !ParsePaletteRange(picImportOptions.paletteRange, first, last))
                {
                    return "--palette-range is first-last, two palette entries of 0 to 255 (for example 80-235)";
                }
            }
            if (viewExport->parsed())
            {
                // Plan section 4.11: --all or one or more views, not both
                // and not neither.
                if ((viewExportOutDirOption->count() > 0) && viewExportOptions.outDir.empty())
                {
                    return "--out-dir needs a folder";
                }
                if (viewExportOptions.all == !viewExportOptions.views.empty())
                {
                    return viewExportOptions.all ? "give --all or --view, not both" : "give --all, or --view with one or more views";
                }
            }
            if (viewImport->parsed())
            {
                // Plan section 4.12: one image (--view and --image, with
                // --loop and --cel or not) or a folder (--in-dir), not both
                // and not neither.
                const bool oneImage = (importViewOption->count() > 0) || (importViewImageOption->count() > 0) || (importLoopOption->count() > 0) || (importViewCelOption->count() > 0);
                const bool folder = (importViewInDirOption->count() > 0);
                if ((importViewOutDirOption->count() > 0) && viewImportOptions.outDir.empty())
                {
                    return "--out-dir needs a folder";
                }
                if (folder && viewImportOptions.inDir.empty())
                {
                    return "--in-dir needs a folder";
                }
                if (oneImage == folder)
                {
                    return folder ? "give --in-dir, or --view with --image, not both" : "give --in-dir, or --view with --image";
                }
                if (oneImage && (viewImportOptions.view.empty() || viewImportOptions.image.empty()))
                {
                    return "an image needs --view and --image";
                }
                if ((importSizeChangesOption->count() > 0) && !folder)
                {
                    return "--allow-size-changes needs --in-dir: the .toml file gives the placement of a cel of a new size";
                }
                const bool replace = (viewImportOptions.palette == "replace");
                const bool range = (importViewRangeOption->count() > 0);
                if (range && !replace)
                {
                    return "--palette-range needs --palette replace";
                }
                if (replace && !range)
                {
                    return "--palette replace needs --palette-range";
                }
                int first = 0;
                int last = 0;
                if (range && !ParsePaletteRange(viewImportOptions.paletteRange, first, last))
                {
                    return "--palette-range is first-last, two palette entries of 0 to 255 (for example 80-235)";
                }
            }
            if (viewAdd->parsed())
            {
                if ((addOutDirOption->count() > 0) && viewAddOptions.outDir.empty())
                {
                    return "--out-dir needs a folder";
                }
                if ((addFromOption->count() > 0) && viewAddOptions.from.empty())
                {
                    return "--from needs a .toml file";
                }
            }
            if (viewPalette->parsed())
            {
                // Plan section 4.9: one action, new colours or no mark.
                const bool colors = (viewColorsFromOption->count() > 0);
                if ((viewOutDirOption->count() > 0) && viewPaletteOptions.outDir.empty())
                {
                    return "--out-dir needs a folder";
                }
                if (colors == viewPaletteOptions.clearUsed)
                {
                    return colors ? "give --colors-from or --clear-used, not both" : "give --colors-from with a BMP file, or --clear-used";
                }
                if (colors && viewPaletteOptions.colorsFrom.empty())
                {
                    return "--colors-from needs a file";
                }
                int first = 0;
                int last = 0;
                if (!ParsePaletteRange(viewPaletteOptions.paletteRange, first, last))
                {
                    return "--palette-range is first-last, two palette entries of 0 to 255 (for example 80-235)";
                }
            }
            if ((functionReportOption->count() > 0) && decompileOptions.functionReport.empty())
            {
                return "--function-report needs a file";
            }
            if (decompile->parsed() && !decompileOptions.functionReport.empty() && !MayOverwriteFunctionReport(decompileOptions.functionReport))
            {
                return "--function-report would overwrite " + decompileOptions.functionReport + ", which is not a function report; give a new file, an empty file, or a function report";
            }
            if (version)
            {
                return std::string();
            }
            if (help->parsed())
            {
                for (const std::string &name : help->remaining())
                {
                    CLI::App *child = nullptr;
                    try
                    {
                        child = helpTopic->get_subcommand(name);
                    }
                    catch (const CLI::OptionNotFound &)
                    {
                    }
                    // "scic help help" shows the help of help: the root help
                    // lists help as a command.
                    if (!child)
                    {
                        return "no help for \"" + name + "\"";
                    }
                    helpTopic = child;
                    helpTopicName += " " + name;
                }
                return std::string();
            }
            if ((baselineOption->count() > 0) && compareOptions.baselineFolder.empty())
            {
                return "--baseline needs a folder";
            }
            if (((compareOutOption->count() > 0) && compareOptions.outFile.empty()) || ((meaningOutOption->count() > 0) && meaningOptions.outFile.empty()) ||
                ((paletteUseOutOption->count() > 0) && paletteUseOptions.outFile.empty()))
            {
                return "--out needs a file";
            }
            if (!script->parsed() && !pic->parsed() && !view->parsed() && !dev->parsed())
            {
                helpAfterUsageError = HelpOf(&app, "scic");
                return "give a command";
            }
            return std::string();
        };
        std::string usageError = findUsageError();

        std::unique_ptr<LogFile> logFile;
        if (!common.logFile.empty())
        {
            logFile = std::make_unique<LogFile>(common.logFile);
            if (!logFile->IsOpen())
            {
                output.Error("cannot open the log file " + common.logFile);
                if (usageError.empty())
                {
                    // Plan section 8: an error before the first script that
                    // is not a usage error.
                    return (int)ExitCode::CannotStart;
                }
                logFile.reset();
            }
        }
        CliOutput logged(console, common, logFile.get());
        if (!usageError.empty())
        {
            logged.Error(usageError);
            if (!helpAfterUsageError.empty())
            {
                logged.HelpAfterError(helpAfterUsageError);
            }
            return (int)ExitCode::Usage;
        }

        if (version)
        {
            logged.Result("scic " SCIC_VERSION_TEXT "\n");
            return (int)ExitCode::Success;
        }
        if (help->parsed())
        {
            logged.Help(HelpOf(helpTopic, helpTopicName));
            return (int)ExitCode::Success;
        }
        if (dev->parsed())
        {
            // No game of the command line: compare-structure reads folders,
            // and compare-meaning and palette-use open their game.
            if (!compareStructure->parsed() && !compareMeaning->parsed() && !paletteUse->parsed())
            {
                logged.Help(HelpOf(dev, "scic dev"));
                return (int)ExitCode::Success;
            }
            const char *const devWork = compareMeaning->parsed() ? "comparing the meaning" : (paletteUse->parsed() ? "reading the palette use" : "comparing the structure");
            sci::Result<ExitCode> compared = sci::Guard(devWork, [&]() -> sci::Result<ExitCode>
            {
                if (compareMeaning->parsed())
                {
                    return RunCompareMeaning(meaningOptions, AbsolutePath(DataFolderOf(common)), logged);
                }
                if (paletteUse->parsed())
                {
                    return RunPaletteUse(paletteUseOptions, AbsolutePath(DataFolderOf(common)), logged);
                }
                return RunCompareStructure(compareOptions, logged);
            });
            if (!compared)
            {
                logged.Error(compared.error().ToString());
                return (int)ExitCodeForStartError(compared.error());
            }
            return (int)*compared;
        }
        if (pic->parsed() && !picList->parsed() && !picExport->parsed() && !picImport->parsed())
        {
            // "scic pic" lists the pic commands.
            logged.Help(HelpOf(pic, "scic pic"));
            return (int)ExitCode::Success;
        }
        if (view->parsed() && !viewList->parsed() && !viewExport->parsed() && !viewImport->parsed() && !viewAdd->parsed() && !viewRemove->parsed() && !viewPalette->parsed())
        {
            // "scic view" lists the view commands.
            logged.Help(HelpOf(view, "scic view"));
            return (int)ExitCode::Success;
        }
        if (script->parsed() && !list->parsed() && !decompile->parsed() && !sco->parsed() && !compile->parsed())
        {
            // Plan section 4.1: "scic script" lists the script commands.
            logged.Help(HelpOf(script, "scic script"));
            return (int)ExitCode::Success;
        }

        // A command that opens a game (plan section 7). An absolute data
        // folder, as for the game folder below: the diagnostics of the
        // headers then have absolute paths.
        std::string dataFolder = AbsolutePath(DataFolderOf(common));
        std::error_code ec;
        // A run that cannot start leaves a function report with no functions,
        // not the report of an older run.
        bool functionReport = decompile->parsed() && !decompileOptions.functionReport.empty();
        auto writeEmptyReport = [&]()
        {
            if (functionReport)
            {
                sci::Status written = WriteEmptyFunctionReport(decompileOptions.functionReport);
                if (!written)
                {
                    logged.Error("the function report: " + written.error().ToString());
                }
            }
        };
        // Only the script commands read the data folder (the headers of
        // include\ and the files of Decompiler\): a pic command runs with no
        // data folder.
        if (script->parsed())
        {
            if (dataFolder.empty() || !fs::exists(fs::path(dataFolder) / "include" / "sci.sh", ec))
            {
                logged.Error(fmt::format("the data folder \"{0}\" has no include\\sci.sh. Give the folder that holds include\\ and Decompiler\\ with --data-dir, or set SCIC_DATA_DIR.", dataFolder));
                writeEmptyReport();
                return (int)ExitCode::CannotStart;
            }
            logged.Detail("The data folder: " + dataFolder);
        }
        if (functionReport)
        {
            // A report that cannot be written stops the command before the
            // decompile.
            sci::Status writable = CheckFunctionReportFile(decompileOptions.functionReport);
            if (!writable)
            {
                logged.Error("the function report: " + writable.error().ToString());
                return (int)ExitCode::WriteFailed;
            }
        }
        ConsoleLogSink sink(logged);
        ScopedCoreLogSink scopedSink(sink);
        SessionOptions sessionOptions;
        sessionOptions.dataFolder = dataFolder;
        GameSession session(sessionOptions);
        // An absolute folder: the paths of the report are then absolute, as
        // the VS Code problem matcher needs (the matcher cannot open a
        // relative path such as ".\src\rm001.sc").
        std::string gameFolder = decompile->parsed() ? decompileOptions.gameFolder :
            (sco->parsed() ? scoOptions.gameFolder : (compile->parsed() ? compileOptions.gameFolder :
            (picList->parsed() ? picListOptions.gameFolder : (picExport->parsed() ? picExportOptions.gameFolder :
            (picImport->parsed() ? picImportOptions.gameFolder : (viewList->parsed() ? viewListOptions.gameFolder :
            (viewExport->parsed() ? viewExportOptions.gameFolder : (viewImport->parsed() ? viewImportOptions.gameFolder :
            (viewPalette->parsed() ? viewPaletteOptions.gameFolder : (viewAdd->parsed() ? viewAddOptions.gameFolder : (viewRemove->parsed() ? viewRemoveOptions.gameFolder : listOptions.gameFolder)))))))))));
        sci::Status opened = session.Open(AbsolutePath(gameFolder));
        if (!opened)
        {
            // Plan section 8: 3, but 2 for a usage error (for example an
            // empty game folder).
            logged.Error("cannot open the game: " + opened.error().ToString());
            writeEmptyReport();
            return (int)ExitCodeForStartError(opened.error());
        }

        sci::Result<ExitCode> ran = sci::Guard("running the command", [&]() -> sci::Result<ExitCode>
        {
            if (decompile->parsed())
            {
                return RunScriptDecompile(session, decompileOptions, common, logged);
            }
            if (sco->parsed())
            {
                return RunScriptSco(session, scoOptions, common, logged);
            }
            if (compile->parsed())
            {
                return RunScriptCompile(session, compileOptions, common, logged);
            }
            if (picList->parsed())
            {
                return RunPicList(session, picListOptions, logged);
            }
            if (picExport->parsed())
            {
                return RunPicExport(session, picExportOptions, common, logged);
            }
            if (picImport->parsed())
            {
                return RunPicImport(session, picImportOptions, common, logged);
            }
            if (viewList->parsed())
            {
                return RunViewList(session, viewListOptions, logged);
            }
            if (viewExport->parsed())
            {
                return RunViewExport(session, viewExportOptions, common, logged);
            }
            if (viewImport->parsed())
            {
                return RunViewImport(session, viewImportOptions, common, logged);
            }
            if (viewPalette->parsed())
            {
                return RunViewPalette(session, viewPaletteOptions, common, logged);
            }
            if (viewAdd->parsed())
            {
                return RunViewAdd(session, viewAddOptions, common, logged);
            }
            if (viewRemove->parsed())
            {
                return RunViewRemove(session, viewRemoveOptions, common, logged);
            }
            return RunScriptList(session, listOptions, logged);
        });
        if (!ran)
        {
            logged.Error(ran.error().ToString());
            return (int)ExitCodeForStartError(ran.error());
        }
        return (int)*ran;
    }

    int CliMain(int argc, char *argv[])
    {
        InstallCrashHandling();
        InstallCancelHandler();
        CrashForATestIfAsked();
        std::vector<std::string> args;
        for (int i = 1; i < argc; i++)
        {
            args.push_back(argv[i]);
        }
        StdConsole console;
        int code = (int)ExitCode::Internal;
        sci::Status ran = sci::Guard("scic", [&]() -> sci::Status
        {
            code = RunCli(args, console);
            return sci::Ok();
        });
        if (!ran)
        {
            console.Err("scic: error: " + ran.error().ToString() + "\n");
            return (int)ExitCode::Internal;
        }
        return code;
    }
}
