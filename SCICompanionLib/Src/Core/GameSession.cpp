#include "stdafx.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ScriptNameMap.h"
#include "SyntaxParser.h"

GameSession::GameSession(const SessionOptions &options, ISCIAppServices *appServices, ResourceRecency *resourceRecency) :
    _options(options),
    _resourceMap(std::make_unique<CResourceMap>(appServices, resourceRecency))
{
    // The compiler's grammars. Without them, every parse fails. The load
    // happens once for the process.
    InitializeSyntaxParsers();
    if (!_options.dataFolder.empty())
    {
        _resourceMap->SetDataFolder(_options.dataFolder);
    }
}

GameSession::~GameSession() = default;

sci::Status GameSession::Open(const std::string &gameFolder)
{
    SCI_TRY(_resourceMap->TryOpen(gameFolder));
    // The script names come from game.ini when it exists, and from the files
    // of src\ (docs/scic-cli/plan.md section 3.4).
    return sci::Guard("reading the script names of " + gameFolder, [&]() -> sci::Status
    {
        SCI_TRY_ASSIGN(ScriptNameMap names, ScriptNameMap::Build(_resourceMap->Helper()));
        _resourceMap->SetScriptNames(std::make_shared<const ScriptNameMap>(std::move(names)));
        return sci::Ok();
    });
}

const GameFolderHelper &GameSession::Helper() const
{
    return _resourceMap->Helper();
}

const SCIVersion &GameSession::Version() const
{
    return _resourceMap->GetSCIVersion();
}
