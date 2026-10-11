# Optional manual checks

These checks are optional and are not a prerequisite for release publication.
Record any untested scenarios explicitly; automated tests cannot establish them.

Record the exact package filename, SHA-256, manifest source/version, OS build,
CPU/RAM, display scale, model/VAD and chunk settings. Use a separate data/output
directory. Do not publish private media, transcripts, URLs or cookies.

On Windows 11 x64, unpack the complete ZIP into a path containing spaces and
Unicode. Use a machine without the Qt/Visual Studio/Python/media-tool SDKs.
Compare `Get-FileHash -Algorithm SHA256 -LiteralPath '.\Transcribe-….zip'` with
the digest for that asset on GitHub Releases, then run `bin\transcribe-gui.exe`.
On Linux, use the actual AppImage and record distribution/glibc and
Wayland/X11 session. Automated clean-container checks complement the desktop test.

1. Start the GUI; verify model download and import, including VAD.
2. Transcribe a local speech file. Observe preview, completion and readable
   TXT/SRT/VTT; copy text and open the result directory.
3. Cancel while processing, inspect retained partial text, retry, then close
   during an active task. Confirm no owned backend/media processes remain.
4. Restart; verify history and exports. Check keyboard navigation, native dialogs,
   minimum window size, light/dark appearance and 100/150/200% display scales.
5. Record default medium + VAD and explicitly selected turbo/chunks separately.
6. If testing a URL, use only an authorized source. Record URL/cache and optional
   authentication results separately from local-file results.

Mark each scenario passed, failed (with reproduction) or not checked. Runner CI
does not establish native dialogs, clipboard or a clean Windows 11 desktop.

The [original Windows checklist](../archive/windows-11-manual-validation.md)
is retained as historical context. For v0.4.0, the maintainer waived Ubuntu
desktop acceptance, cookies and a 10–15 minute recording; those checks were not
claimed passed. See the [release journal](../releases/v0.4.0/validation.md).
Each release records its actual testing scope and remaining limitations.
