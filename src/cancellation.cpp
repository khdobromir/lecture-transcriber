#include "cancellation.hpp"
#include "process.hpp"
#include <utility>

namespace transcribe {
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
CancellationToken& cancellation_token() { static CancellationToken token; return token; }
void CancellationToken::request(int signal) noexcept {
    int expected = 0;
    if (!state_.compare_exchange_strong(expected, signal) && expected > 0) repeated_.store(true);
}
int CancellationToken::signal() const noexcept { const int state = state_.load(); return state > 0 ? state : 0; }
void CancellationToken::reset() noexcept { state_.store(0); repeated_.store(false); }
CancellationReason CancellationToken::reason() const noexcept {
    const int code = signal();
    return !code ? CancellationReason::none : code == 13 ? CancellationReason::connection_lost : code == 2 ? CancellationReason::user : CancellationReason::signal;
}
void CancellationToken::commit() {
    poll();
    int expected = 0;
    if (!state_.compare_exchange_strong(expected, -1)) check_cancelled();
}
int cancellation_signal() { return cancellation_token().signal(); }
void check_cancelled() {
    auto& token = cancellation_token();
    token.poll();
    if (const int signal = token.signal()) throw ProcessError("Обработка прервана сигналом " + std::to_string(signal), 128 + signal);
}
void commit_completion() { cancellation_token().commit(); }
}
