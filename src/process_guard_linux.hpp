#pragma once
#include <string>
#include <vector>
#include <sys/types.h>

namespace transcribe {
struct GuardedChild { pid_t guardian = -1, group = -1; int lease = -1; };
// An independent supervisor owns/reaps the tool group and watches the owner's
// CLOEXEC lifetime pipe. Owner death closes that pipe even after SIGKILL.
GuardedChild spawn_guarded(const std::vector<std::string>& args, int output, int errors);
}
