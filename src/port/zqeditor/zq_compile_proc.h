// Running a ZScript compiler program over upstream's pipe protocol (zq_compile_proc.cpp).

#ifndef ZQ_COMPILE_PROC_H
#define ZQ_COMPILE_PROC_H

#include <stdint.h>
#include <string>
#include <vector>

enum { ZQ_PROC_NOT_RUN = -100000, ZQ_PROC_TIMEOUT = -100001, ZQ_PROC_DIED = -100002 };

// Starts exe with args in dir (empty: ZQuest's own), answers its messages, and returns its
// result (0: compiled), or one of the ZQ_PROC_ codes with why set. on_message is called for
// each message code before it is answered (the text is in the -console file). timeout_s: how
// long a step may take (0: no limit). *cancel set to true stops the compiler.
int zq_run_linked_compiler(const std::string &exe, const std::vector<std::string> &args, const std::string &dir,
                           int timeout_s, volatile bool *cancel, void (*on_message)(int code, void *user), void *user,
                           std::string &why);

// The folder ZQuest's program is in (with a separator at the end), the system's folder for
// temporary files (likewise), and this process's id.
std::string zq_program_dir();
std::string zq_temp_dir();
long zq_process_id();

#endif
