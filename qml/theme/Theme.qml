pragma Singleton
import QtQuick

// Central 10-foot design tokens. Large type, generous spacing, one obvious focus
// colour. Dark by default (living-room friendly).
QtObject {
    id: theme

    // Palette
    readonly property color background:   "#0e1013"
    readonly property color surface:      "#1a1e24"
    readonly property color surfaceAlt:   "#232830"
    readonly property color textPrimary:  "#f2f4f7"
    readonly property color textMuted:    "#9aa4b2"
    readonly property color accent:       "#4fa3ff"
    readonly property color focusRing:    "#7cc0ff"
    readonly property color progress:     "#4fd18b"
    readonly property color danger:       "#ff6b6b"

    // Type scale (px) — sized for viewing distance.
    readonly property int fontHuge:   48
    readonly property int fontTitle:  34
    readonly property int fontHeader: 26
    readonly property int fontBody:   20
    readonly property int fontSmall:  16

    // Spacing / geometry
    readonly property int spacingSmall:  8
    readonly property int spacing:       16
    readonly property int spacingLarge:  32
    readonly property int radius:        10
    readonly property int focusBorder:   4

    readonly property int cardWidth:   240
    readonly property int cardHeight:  360
    // Reserved room around a card so the focus zoom never clips or bumps labels.
    readonly property int focusPad:    18
    readonly property int cardLabelH:  54
    readonly property int cardCellW:   cardWidth + 2 * focusPad
    readonly property int cardCellH:   cardHeight + cardLabelH + 2 * focusPad

    readonly property int animFast: 120
    readonly property int animNormal: 200
}
