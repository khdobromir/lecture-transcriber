# Application icon

The maintainer supplied root `icon.png`; keep its original bytes as the canonical
artwork. README and the Qt resource use it directly. The Windows ICO resource is
embedded in both CLI and GUI executables. Linux installs a 512-pixel PNG under
the hicolor theme; linuxdeploy uses that PNG and AppImage `.DirIcon` points to it.

Derived files were generated with ImageMagick (not required to build/run):

```sh
magick icon.png -resize 512x512 -strip packaging/linux/transcribe-gui.png
magick icon.png -define icon:auto-resize=256,128,64,48,32,16 -strip packaging/windows/transcribe.ico
```

Keep both generated files in Git so Windows/Linux builds do not need a converter.
The desktop entry and Qt desktop file name remain `transcribe-gui`.
