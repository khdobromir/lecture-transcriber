# Material You interface

Transcribe uses Qt Quick Controls Material as the startup style, with application
components for a shared Material 3 palette. The style never changes after QML
registration. Appearance and language updates preserve the window and mounted
pages; the backend owns the editable launch draft, active operation and selected
history result. Cookie fields remain in memory and are not persisted.

## Appearance

Settings offers system/light/dark modes, wallpaper, a local image, four seed
presets, a HEX color, and reduced motion. Wallpaper and system mode are separate
integrations. The first-run defaults are wallpaper and system mode. Missing
sources preserve the last successful seed across restarts, falling back to
`#6750a4`. Palette calculations are local and never upload images.

The pinned Material Color Utilities sources and documented portability patches
are under `third_party/material-color-utilities`. The algorithm composites alpha
on white in sRGB, fits inside 128 × 128, quantizes with Celebi (128 colors), scores
one candidate, and builds light/dark Tonal Spot schemes at contrast 0.0. Decoder
subsampling is used where supported. All semantic colors come from the same
scheme; selected text and keyboard focus have explicit colors.

The cache contains at most 16 successful seeds and no original images. Its
identity includes content SHA-256, library revision and algorithm version 1.
Settings, seed and selected source persist under `ui/appearance`.

Supported images: PNG, JPEG, WebP, GIF, BMP. Animated formats use only frame zero.
Limits are 64 MiB input, 32 million pixels, 16384 pixels per side and a 128 MiB
QImageReader allocation ceiling. This is not a total-process RSS guarantee.
One worker and one newest pending request bound concurrency. Events coalesce for
300 ms; superseded results cannot replace the current theme. Transient failures
retry after 5, 30 and then 60 seconds even when metadata is unchanged.

## Desktop integration

* Omarchy reads `~/.local/state/omarchy/current/background`. The link, target and
  containing directories are watched and re-armed after replacement. A selected
  still stays authoritative even when it has a paired intro video. A selected
  video uses one frame through the bundled FFprobe/FFmpeg, with a five-second
  deadline and cancellation. No wallpaper playback is added.
* Windows selects the primary display explicitly through native monitor
  geometry and `IDesktopWallpaper`. Different monitor wallpapers and slideshows
  do not use `GetWallpaper(nullptr)`. A solid background uses the desktop color.
  Native setting/display notifications, focus and a 30-second poll refresh the
  source. Moving the app window does not select another monitor's palette.
* Other Linux desktops use manual picture/color selection. The app never writes
  desktop configuration.

System mode uses `QStyleHints::colorScheme`. If unknown on Omarchy, it follows
`mode`, `theme_type`, `light.mode`, then background luminance in the current theme.
Unknown mode falls back to dark. Explicit light/dark overrides detection.

## Layout and component policy

The supported minimum client area is 720 × 420 logical pixels. All pages scroll.
Navigation switches between 80-pixel icons and a 216-pixel labeled rail at window
width 1000. Transcription uses two columns only at content width 844 or wider
(400 + 24 + 420). Active operation progress and cancellation stay in the shell.
The source, result and history selection are independent.

| Component | Implementation and state coverage |
| --- | --- |
| Button / navigation | Qt Quick Templates, semantic fill/ink, hover, press, checked, disabled, focus |
| Text field / transcript | Templates, outlined/container surface, selection and focus; no floating-label animation |
| Checkbox / switch | Templates, semantic indicator; switch movement respects reduced motion |
| Combo / spin box | Templates with public popup/delegate/indicator APIs; no Material private helpers |
| List item | Public ItemDelegate content/background; semantic hover, selection and focus |
| Dialog / card | Public Dialog/Pane with semantic surfaces; dialog enter/exit transitions disabled |
| Progress | Template with semantic track; reduced motion uses a static indeterminate mark plus status text |

`ComponentGallery.qml` and the GUI tests exercise normal, hover, press, focus,
selection, disabled and reduced-motion states in both themes. Decoration opacity
is used for disabled controls; text contrast thresholds concern enabled content.

## Build and acceptance

Use the existing GUI build presets. Qt LinguistTools is required to update and
validate the English catalog. Test targets include `test_theme`, `test_qml`, and
the existing controller, process, translation and real-speech suites. The
`TRANSCRIBE_GUI_VISUAL_DIRECTORY` variable saves reproducible component/page
screenshots using synthetic input.

MSVC builds retain compiled QML bytecode and use Qt's public `--only-bytecode`
cache-generator option. Qt 6.8's script-to-C++ output otherwise triggers C4702
inside SDK conversion templates. Application C++ still uses `/W4` and `/WX`;
QML lint and runtime tests remain enabled.

The local acceptance platform is Omarchy x86_64 / Wayland. Record the actual OS,
Hyprland, Qt, display resolution, scale and client size. Exercise both languages,
both modes, 100/150/200% scaling, 720 × 420, the navigation threshold in both
directions, and the two-column threshold. The release-package validation for
Windows 11 x64 remains open until it runs on an actual Windows machine; mocks
and a Linux build do not constitute Windows acceptance.

`scripts/package-linux-preview.py` builds a diagnostic AppImage using an
explicitly SHA-256-verified release as the source of unchanged bundled tools,
a Qt 6.8.3 build with `CMAKE_SKIP_RPATH=ON`, and the locked appimagetool. It copies
Material/QML/image plugins, resolves runtime dependencies, includes the new
license, and records the actual glibc requirement and a worktree fingerprint.
It does not inherit compatibility claims from the base bundle or modify the
installed application. The ordinary release builders remain separate.

A finished acceptance report must identify the exact package SHA-256 and record
all tested and unverified scenarios, including real speech and TXT/SRT/VTT.
