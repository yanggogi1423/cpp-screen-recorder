import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string symbol: "panel"
    property bool danger: false
    implicitWidth: 32
    implicitHeight: 32
    padding: 0
    hoverEnabled: true
    Accessible.name: text
    ToolTip.visible: hovered
    ToolTip.delay: 450
    ToolTip.text: text
    opacity: enabled ? 1 : 0.35
    background: Rectangle {
        radius: 7
        color: control.down ? Theme.pressed : control.hovered ? (control.danger ? "#b63832" : Theme.hover) : "transparent"
    }
    contentItem: Canvas {
        id: drawing
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.strokeStyle = control.hovered && control.danger ? "#ffffff" : "#cccccc";
            ctx.lineWidth = 1.2;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            var x = width / 2, y = height / 2;
            ctx.beginPath();
            if (control.symbol === "close") {
                ctx.moveTo(x - 4, y - 4);
                ctx.lineTo(x + 4, y + 4);
                ctx.moveTo(x + 4, y - 4);
                ctx.lineTo(x - 4, y + 4);
            } else if (control.symbol === "minimize") {
                ctx.moveTo(x - 4, y);
                ctx.lineTo(x + 4, y);
            } else if (control.symbol === "maximize") {
                ctx.rect(x - 4, y - 4, 8, 8);
            } else if (control.symbol === "restore") {
                ctx.rect(x - 5, y - 2, 7, 7);
                ctx.moveTo(x - 2, y - 5);
                ctx.lineTo(x + 5, y - 5);
                ctx.lineTo(x + 5, y + 2);
            } else if (control.symbol === "panel") {
                ctx.roundedRect(x - 7, y - 6, 14, 12, 2, 2);
                ctx.moveTo(x + 2, y - 6);
                ctx.lineTo(x + 2, y + 6);
            } else if (control.symbol === "compact") {
                ctx.rect(x - 7, y - 5, 14, 10);
                ctx.moveTo(x - 4, y + 1);
                ctx.lineTo(x + 4, y + 1);
            } else if (control.symbol === "folder") {
                ctx.moveTo(x - 7, y - 5);
                ctx.lineTo(x - 2, y - 5);
                ctx.lineTo(x, y - 3);
                ctx.lineTo(x + 7, y - 3);
                ctx.lineTo(x + 7, y + 6);
                ctx.lineTo(x - 7, y + 6);
                ctx.closePath();
            } else if (control.symbol === "chevron") {
                ctx.moveTo(x - 4, y - 2);
                ctx.lineTo(x, y + 2);
                ctx.lineTo(x + 4, y - 2);
            } else {
                ctx.moveTo(x - 7, y - 3);
                ctx.lineTo(x - 4, y - 3);
                ctx.lineTo(x, y - 6);
                ctx.lineTo(x, y + 6);
                ctx.lineTo(x - 4, y + 3);
                ctx.lineTo(x - 7, y + 3);
                ctx.closePath();
                if (control.symbol === "muted") {
                    ctx.moveTo(x + 3, y - 3);
                    ctx.lineTo(x + 8, y + 3);
                    ctx.moveTo(x + 8, y - 3);
                    ctx.lineTo(x + 3, y + 3);
                } else {
                    ctx.moveTo(x + 4, y - 4);
                    ctx.quadraticCurveTo(x + 9, y, x + 4, y + 4);
                }
            }
            ctx.stroke();
        }
        Connections {
            target: control
            function onHoveredChanged() {
                drawing.requestPaint();
            }
            function onSymbolChanged() {
                drawing.requestPaint();
            }
        }
    }
}
