Transcribe for Windows 11 x64

1. Extract the entire ZIP to a short path, for example C:\Apps\Transcribe.
2. Run bin\transcribe-gui.exe. No administrator rights are needed.
3. Open the Models tab and download medium (or small/turbo) and VAD.
   Models are stored separately in %LOCALAPPDATA%\Transcribe\models.
4. Choose a local audio/video file or paste a URL and start transcription.

Results are saved under %USERPROFILE%\Transcriptions by default.
Each task has its own folder containing TXT/SRT/VTT, logs, source.txt and
result.json. Cancel keeps partial text and working audio. Closing an active
window asks for confirmation and waits for cancellation.

CLI example (PowerShell):
  .\bin\transcribe.exe --out C:\Transcriptions "C:\Media\Lecture.mp4"
Keep the entire extracted directory tree, including bin\tools, Qt DLLs,
plugins and QML files. Do not move just the EXE. Models and user results are never part of this ZIP.

Source, build instructions and third-party notices: THIRD-PARTY.md.
package-manifest.json records source revision, build status and file hashes.
