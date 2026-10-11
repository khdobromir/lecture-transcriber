# GUI states and worker ownership

[Architecture](overview.md) · [Protocol](protocol-v1.md)

QML uses Qt Quick Controls/Layouts for presentation and allowed actions; C++ owns
tasks, models and files. Backend is a required QML property. List models/QObjects
belong to the GUI thread. [states.hpp](../../gui/states.hpp) is the transition
source of truth; [backend.cpp](../../gui/backend.cpp) implements task orchestration.

## Current task versus saved result

| From | Allowed next task states |
|---|---|
| idle/completed/interrupted/failed | starting |
| starting | running/cancel_requested/failed/interrupted |
| running | cancel_requested/finalizing/failed/interrupted |
| cancel_requested | finalizing/failed/interrupted |
| finalizing | completed/cancel_requested/failed/interrupted |

Self-transitions are allowed. Busy is derived from state; a new task is forbidden
while a task/model operation is active. Cancel becomes unavailable after a terminal
event. A backend with coherent success enters finalizing while an independent
worker verifies manifest/exports. Late cancellation does not revoke a confirmed
commit. Ordinary close asynchronously waits for task/model settlement.

`taskResultDirectory` belongs to the current run. `resultDirectory`, preview and
`selectedStatus` belong to the selected saved result. History can open during a
task, then return to its result. Preflight/FailedToStart affect the current task
only. Retry keeps parameters in memory, not cookies in QSettings. Task directories
and logs have separate actions. A failed connection does not invalidate a saved
completed manifest; history may still discover it independently.

Run, selection and scan generations reject stale callbacks. Preview/history
workers receive snapshots and cooperative cancellation tokens. Outstanding remains
true until the finished callback is handled in the GUI thread: a finished worker
with a queued callback cannot be reused early. Old callbacks cannot overwrite a
new selection/scan. TXT reading is limited to 512 KiB, with an explained tail view
and separate full-file action. Preview/history errors and task warnings retain
separate contexts.

## Model installation

[ModelManager](../../gui/models.cpp) has idle, waiting_lock, checking_existing,
downloading, verifying, publishing, cancelling, ready, failed and cancelled
states. Inactive states start through waiting_lock. Active phases may advance;
import can skip short phases because the UI receives snapshots. Cancelling only
settles to actual ready/failed/cancelled (or remains cancelling).

Hashing/import/backup/replacement run in workers. Cancellation is checked while
waiting for locks, between read/copy blocks and replacement retries. Only GUI
thread code uses QNetworkReply/watchers/QObjects. HTTP callbacks check reply
identity and operation generation. Publication and model selection finish under
`.install.lock`; terminal states release ownership only after workers finish.
A successful commit retains ready under late cancellation.

An installed corrupt model is not deleted. Explicit backup-and-recover first
copies it under the installation lock, then hashes/publishes a new file.
Network/hash/write errors preserve the original and completed backup. An invalid
import cannot replace the installed model.

Destructors are the final safeguard: request cooperative cancellation, disconnect
callbacks and join workers, catching completed-work exceptions. Ordinary UI close
does not block on joining inside its event handler. [workers.hpp](../../gui/workers.hpp)
unwraps Qt worker exceptions for meaningful diagnostics. Tests inject data-only
Readers/Workers hooks and barriers, not a second production orchestration layer.

## UI and verification

Standard Controls support keyboard interaction: Ctrl+O, Ctrl+Enter, Ctrl+Q and F5.
Native dialogs return focus. Task/model errors remain selectable within bounded
ScrollViews; paths, transcript and history messages use plain text. Minimum window
size is 720×600 and export actions wrap.

[Controller tests](../../tests/test_gui.cpp) and [QML tests](../../tests/test_qml.cpp)
cover guards, preflight, FailedToStart/retry, A→B→A selection, stale scans, worker
exceptions/teardown, hash/publication cancellation, late commit, backup recovery,
close during task/model lock and Tab/Shift+Tab. Native dialogs/portal/clipboard and
user DPI are separately recorded in [manual acceptance](../testing/manual.md).
