#include "stdafx.h"
#include "GameSession.h"
#include "ResourceMap.h"
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
    return _resourceMap->TryOpen(gameFolder);
}

const GameFolderHelper &GameSession::Helper() const
{
    return _resourceMap->Helper();
}

const SCIVersion &GameSession::Version() const
{
    return _resourceMap->GetSCIVersion();
}
