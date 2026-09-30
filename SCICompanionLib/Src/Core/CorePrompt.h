#pragma once

// A message for the user, or a question, from engine code that also runs
// with no GUI (the command line, the unit tests).
//
// The GUI installs a handler that shows a message box. With no handler, or
// when the handler shows nothing, the text goes to the core log (CoreLog.h),
// and a question gets the answer that changes nothing: IDCANCEL for a
// question with Cancel, IDNO for a yes/no question, IDABORT for
// abort/retry/ignore, else IDOK.

#include <string>

// Shows the text with the MB_* type and returns the ID* answer, or returns 0
// when it shows nothing (for example before the main window exists).
using MessageBoxHandler = int (*)(const std::string &text, unsigned int type);

// Installs the handler (null removes it) and returns the handler before it.
MessageBoxHandler SetMessageBoxHandler(MessageBoxHandler handler);

int SafeMessageBox(const std::string &text, unsigned int type);
