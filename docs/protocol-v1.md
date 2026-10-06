# GUI ↔ CLI: protocol v1

`transcribe --machine` writes UTF-8 JSON objects, one object per LF-terminated
line, to stdout. Stderr contains diagnostics. The control channel accepts
`{"type":"cancel"}` followed by LF. EOF or malformed control requests cancellation
with connection-loss code 141. Recognized speech is written to result files,
not protocol stdout.

`ProtocolParser` in the C++ core validates the same serialized events consumed
by the Qt GUI. Unknown event types are errors in v1. Extra fields in a known
event are allowed and never control state. A semantic change requires another
protocol version. Maximum event line size, excluding LF, is 262144 bytes; this
is a per-line bound, not a bound on the number of events or task duration.

| Type | Mandatory fields in addition to `protocol: 1`, string `type` |
|---|---|
| hello | None; exactly once, first |
| result | String `stage`, `message`, absolute string `result`; stage is `temporary` or empty |
| stage | String `stage`, `message`, `result`; stage is local/probe/cache/download/prepare/split/recognize/merge |
| progress | String `stage: recognize`, `message`, `result`; finite number `fraction` in [0,1], integers `finished >= 0`, `total > 0`, `finished <= total`, `eta_seconds` null or finite number >= 0 |
| warning | String `stage`, `message`, `result` |
| finalizing | String `stage`, `message`, `result` |
| completed | String `stage`, `message`, `result`, `status: completed`, integer `code: 0` |
| failed | String `stage`, `message`, `result`, status failed/interrupted and nonzero signed 32-bit integer `code`; interrupted uses 128 + cancellation signal (129–192) |

Every non-hello event contains the common fields even when a string is empty.
Result paths contain no NUL and are absolute whenever nonempty. Only failed
can have an empty directory, as in preflight failure. Negative native Windows
exception exit codes are preserved. A URL run first announces a temporary result
directory and then a final directory after naming it. Only a result event can
make that change. Once final, the directory cannot change; other events refer
to the announced directory. Failure before the first announcement can report
its already-created directory. Events after either terminal event, including
a repeated terminal/hello, are errors.

Within recognize, fraction and finished do not decrease and total remains
constant. A stage change resets displayed progress and ETA. Fraction 1 does
not imply successful publication. Success requires completed/code 0, normal
process exit 0, complete protocol input, a matching completed result.json and
all TXT/SRT/VTT exports on disk. A failed protocol/connection cannot be repaired
by exit 0; saved results remain separately discoverable through history.

The GUI consumes at most 64 events and 65536 newly read bytes per event-loop
turn. It continues draining after process exit before deciding the outcome.
Malformed UTF-8, incomplete final lines, missing/wrong fields, incompatible
versions, arbitrary directory changes and contradictory terminal values fail
the current task.

Cancellation requested before the core's atomic completion commit interrupts
the run and retains recovery data. Requests after that commit preserve success.
The GUI request and backend-confirmed interruption are separate observations.
Ordinary close uses asynchronous cancellation and completion notifications.
Linux tools have an independent supervisor that owns their process group and
reaps descendants. A CLOEXEC lifetime pipe lets it observe owner SIGKILL and
kill the managed group without relying on the dead CLI. Child fork paths use
preallocated data and system calls before exec. Windows uses kill-on-close Job
Objects. Processes that deliberately escape their Linux group are outside
this managed-tools contract.

References: [Qt QProcess](https://doc.qt.io/qt-6.8/qprocess.html),
[Qt QFutureWatcher](https://doc.qt.io/qt-6.8/qfuturewatcher.html),
[Linux fork](https://man7.org/linux/man-pages/man2/fork.2.html),
[Linux close_range](https://man7.org/linux/man-pages/man2/close_range.2.html).
