# ADR 0001: CLI process boundary

Status: accepted; documents the existing implementation as inspected on 2026-10-11.

## Context

Users need both an independent CLI and a desktop GUI. Recognition and media
tools are long-running external processes with diagnostics, cancellation and
partial output. Linking Qt into the core or running a separate orchestration
implementation for the GUI would duplicate behavior and increase coupling.

## Decision

Keep `transcribe_core` Qt-free. The CLI runs one task per process. The GUI starts
that CLI with QProcess and consumes versioned UTF-8 JSONL, with stdin cancellation.
The GUI handles presentation/model/history work; the shared pipeline owns task
semantics. Do not add a persistent service, queue or plugin framework to document
this boundary.

## Consequences

CLI remains usable without Qt. Native process supervision and pipeline tests
serve both interfaces; GUI/CLI integration tests exercise the real boundary.
Streaming needs bounded buffering, strict protocol validation and separate saved
result versus current task identity. Completion needs protocol, exit, manifest
and exports to agree. Protocol changes need explicit version/compatibility work;
EOF/cancel and late-commit races cannot be reduced to a boolean busy flag.

Evidence: [CMake](../../../CMakeLists.txt), [Backend](../../../gui/backend.cpp),
[protocol](../protocol-v1.md), [integration tests](../../../tests/test_gui_integration.cpp).
