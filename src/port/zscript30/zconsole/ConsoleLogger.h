#ifndef ZSCRIPT30_CONSOLELOGGER_H_
#define ZSCRIPT30_CONSOLELOGGER_H_

// zscript30: stands in for upstream's zconsole/ConsoleLogger.h. The compiler uses
// nothing of the console window itself (that is the editor's side, which needs
// Allegro); it only needs what that header brought in: the pipe to the editor
// (io_manager, child_process_handler) from base/process_management.h.
#include "base/ints.h"
#include "base/process_management.h"
#include <string>

#endif
