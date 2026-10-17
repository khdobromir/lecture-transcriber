pragma Singleton
import QtQuick
import Transcribe

QtObject {
    property ThemeController controller: null
    readonly property var colors: controller ? controller.colors : ({
        surface: "#141218", onSurface: "#e6e0e9", surfaceContainerLow: "#1d1b20",
        surfaceContainer: "#211f26", surfaceContainerHigh: "#2b2930", onSurfaceVariant: "#cac4d0",
        primary: "#d0bcff", onPrimary: "#381e72", primaryContainer: "#4f378b", onPrimaryContainer: "#eaddff",
        secondary: "#ccc2dc", onSecondary: "#332d41", secondaryContainer: "#4a4458", onSecondaryContainer: "#e8def8",
        outline: "#938f99", outlineVariant: "#49454f", error: "#f2b8b5", onError: "#601410"
    })
    readonly property bool reducedMotion: controller ? controller.reducedMotion : false
    readonly property int duration: reducedMotion ? 0 : 160
}
