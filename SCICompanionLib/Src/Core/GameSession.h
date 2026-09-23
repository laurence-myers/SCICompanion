#pragma once

// A GameSession is one open game and the options of its host. The command
// line makes one for each run. The GUI's AppState owns one and forwards
// GetResourceMap and GetVersion to it. This interface uses no MFC type.
// See docs/scic-cli/plan.md, section 3.2.

#include "Result.h"
#include <memory>
#include <string>

class CResourceMap;
class GameFolderHelper;
class IClassHints;
class ISCIAppServices;
class ResourceRecency;
struct SCIVersion;

struct SessionOptions
{
    // The folder that holds include\ and Decompiler\. Empty: the folder of
    // the program.
    std::string dataFolder;
    // Warn about an instance that nothing refers to (the GUI default).
    bool warnOnUnusedInstances = true;
};

class GameSession
{
public:
    // The GUI passes its app services and its resource recency. The command
    // line and the tests pass neither.
    explicit GameSession(const SessionOptions &options = SessionOptions(), ISCIAppServices *appServices = nullptr, ResourceRecency *resourceRecency = nullptr);
    ~GameSession();
    GameSession(const GameSession &) = delete;
    GameSession &operator=(const GameSession &) = delete;

    // Opens the game in the folder. No dialog and no exception: a failure
    // comes back as an error, and then no game is open.
    sci::Status Open(const std::string &gameFolder);

    CResourceMap &ResourceMap() { return *_resourceMap; }
    const CResourceMap &ResourceMap() const { return *_resourceMap; }
    const GameFolderHelper &Helper() const;
    const SCIVersion &Version() const;
    const SessionOptions &Options() const { return _options; }
    void SetWarnOnUnusedInstances(bool warn) { _options.warnOnUnusedInstances = warn; }

    // Optional hints for compile error messages. Null by default; the GUI
    // sets its class browser. The session does not own them.
    IClassHints *ClassHints() const { return _classHints; }
    void SetClassHints(IClassHints *classHints) { _classHints = classHints; }

private:
    SessionOptions _options;
    std::unique_ptr<CResourceMap> _resourceMap;
    IClassHints *_classHints = nullptr;
};
