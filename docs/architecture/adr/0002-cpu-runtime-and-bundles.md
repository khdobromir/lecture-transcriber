# ADR 0002: CPU runtime and complete bundles

Status: accepted; documents the existing runtime/distribution constraints on 2026-10-11.

## Context

Local lecture transcription should work on ordinary Linux/Windows CPU hardware.
Native orchestration, memory/resource ownership and predictable installation are
project goals. Whisper models are large, change independently and remain user data.
FFmpeg/yt-dlp solve media/extractor problems but have their own version/license
and trust boundaries.

## Decision

Keep native C++23/Qt and external whisper.cpp CPU processes. Use explicit
threads/chunks/jobs, one chunk by default, and runtime CPU dispatch in packaged
engines instead of requiring the builder's CPU features. Do not introduce a
Python/web backend, GPU dependency or alternative engine without demonstrated need.

Binary AppImage/ZIP distributions include tested runtime/tool bytes, marker,
provenance, notices and associated source inputs; models stay outside and GUI
download/import verifies known-preset SHA-256. A marked incomplete bundle fails
closed, rather than discovering arbitrary system tools/DLLs. The source-install
route can use system media tools and a separately built pinned engine.

## Consequences

Offline local recognition works after model preparation; first-run model download
and URLs need network. More parallel workers load more model memory. Native
process/DLL/environment hardening and clean final-artifact smoke remain necessary.
Moving download URLs still require pinned hashes, reviewed source mappings and
updated complete artifacts. Unknown custom model paths do not inherit preset
hash assurance. ASR accuracy and universal performance are not guaranteed.

Model/tool/library licensing remains upstream-owned. Complete source materials
and relinking/replacement instructions are distribution responsibilities, not
reasons to switch the application's architecture.

Evidence: [platform/bundle resolution](../../../src/platform.cpp),
[CLI preflight](../../../src/cli.cpp), [models](../../../gui/models.cpp),
[Linux pins](../../../packaging/linux/dependencies.json),
[Windows pins](../../../packaging/windows/dependencies.json),
[source materials](../../build/dependency-source-materials.md).
