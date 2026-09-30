#pragma once

// The exception boundary (sci::Guard) of the core library does not know
// MFC. This mapper gives it MFC's CException*: a CFileException gives Io, a
// CMemoryException gives "out of memory", any other CException gives
// Internal with its text. It deletes the CException.
bool MapMfcException(sci::Error &error);

// Installs MapMfcException as the foreign exception mapper of the core
// library. AppState calls it.
void InstallMfcExceptionMapper();
