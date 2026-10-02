#pragma once

// The game that the GUI has open. A GUI file that needs only the game
// includes this header, not AppState.h, so a change of AppState.h does not
// compile it again. The engine (SCICompanionCore) does not use these: it takes
// the session, the resource map or the helper as a parameter.

class GameSession;
class CResourceMap;
struct SCIVersion;

// The session of the GUI (AppState::GetSession).
GameSession &AppSession();

// The resource map of the session.
CResourceMap &AppResourceMap();

// The SCI version of the session.
const SCIVersion &AppVersion();
