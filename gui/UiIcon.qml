import QtQuick
import Transcribe

Canvas {
    id: icon
    property int symbol: 0
    property color ink: UiStyle.colors.onSurface
    implicitWidth: 24; implicitHeight: 24
    onInkChanged: requestPaint()
    onSymbolChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = ink
        ctx.lineWidth = 1.8
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()
        if (symbol === 0) {
            ctx.moveTo(5, 5); ctx.lineTo(19, 5)
            ctx.moveTo(5, 10); ctx.lineTo(19, 10)
            ctx.moveTo(5, 15); ctx.lineTo(15, 15)
            ctx.moveTo(5, 20); ctx.lineTo(12, 20)
        } else if (symbol === 1) {
            ctx.arc(12, 12, 8, -2.7, 2.7)
            ctx.moveTo(12, 7); ctx.lineTo(12, 12); ctx.lineTo(16, 14)
            ctx.moveTo(3, 4); ctx.lineTo(3, 9); ctx.lineTo(8, 9)
        } else if (symbol === 2) {
            ctx.moveTo(12, 3); ctx.lineTo(21, 8); ctx.lineTo(12, 13); ctx.lineTo(3, 8); ctx.closePath()
            ctx.moveTo(3, 13); ctx.lineTo(12, 18); ctx.lineTo(21, 13)
            ctx.moveTo(3, 18); ctx.lineTo(12, 23); ctx.lineTo(21, 18)
        } else {
            ctx.arc(12, 12, 4, 0, Math.PI * 2)
            for (let i = 0; i < 8; ++i) {
                const angle = i * Math.PI / 4
                ctx.moveTo(12 + Math.cos(angle) * 8, 12 + Math.sin(angle) * 8)
                ctx.lineTo(12 + Math.cos(angle) * 10, 12 + Math.sin(angle) * 10)
            }
        }
        ctx.stroke()
    }
}
