#pragma once
#include <cstdint>

enum class TaskState : std::uint8_t { idle, starting, running, cancel_requested, finalizing, completed, interrupted, failed };
enum class ModelState : std::uint8_t { idle, waiting_lock, checking_existing, downloading, verifying, publishing, cancelling, ready, failed, cancelled };
constexpr bool taskBusy(TaskState state) {
    return state == TaskState::starting || state == TaskState::running || state == TaskState::cancel_requested || state == TaskState::finalizing;
}
constexpr bool modelBusy(ModelState state) {
    return state == ModelState::waiting_lock || state == ModelState::checking_existing || state == ModelState::downloading ||
        state == ModelState::verifying || state == ModelState::publishing || state == ModelState::cancelling;
}
constexpr bool transitionAllowed(TaskState from, TaskState to) {
    if (from == to) return true;
    if (!taskBusy(from)) return to == TaskState::starting;
    if (to == TaskState::failed || to == TaskState::interrupted) return true;
    if (to == TaskState::cancel_requested) return from != TaskState::cancel_requested;
    if (to == TaskState::finalizing) return from != TaskState::starting;
    if (to == TaskState::running) return from == TaskState::starting;
    return to == TaskState::completed && from == TaskState::finalizing;
}
constexpr bool transitionAllowed(ModelState from, ModelState to) {
    if (from == to) return true;
    if (!modelBusy(from)) return to == ModelState::waiting_lock;
    if (to == ModelState::ready || to == ModelState::failed || to == ModelState::cancelled || to == ModelState::cancelling) return true;
    // An import reports worker phase snapshots; quick intermediate phases may
    // already have finished when the GUI receives the next snapshot.
    return from != ModelState::cancelling && modelBusy(to) && to != ModelState::waiting_lock;
}
