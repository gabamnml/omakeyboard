pragma Singleton
import QtQuick

// Design tokens, taken from Omarchy rather than invented.
//
// Colours come from the active theme's colors.toml. The type scale and the
// spacing scale come from that theme's shell.toml, with the same rem-root
// arrangement the Omarchy shell uses: `base-size` is the root and every size
// derives from it. Corner rounding and the gap to a screen edge are mirrored
// from the user's own Hyprland configuration, which is what makes an Omarchy
// desktop feel of a piece.
QtObject {
    id: root

    // ---------------------------------------------------------- colour
    readonly property color background: theme.background
    readonly property color surface: theme.surface
    readonly property color elevated: theme.elevated
    readonly property color foreground: theme.foreground
    readonly property color muted: theme.mutedForeground
    readonly property color accent: theme.accent
    readonly property color selection: theme.selection
    readonly property color border: theme.border
    readonly property color error: theme.error
    readonly property color warning: theme.warning
    readonly property color success: theme.success
    readonly property bool dark: theme.darkMode

    // ---------------------------------------------------------- typography
    // The shell's scale: caption .833, body-small .917, body 1, subtitle
    // 1.083, title 1.167, heading 1.333, display 2.
    readonly property int fontBase: theme.fontBaseSize
    function fontPx(multiplier) {
        return Math.max(1, Math.round(root.fontBase * multiplier))
    }

    readonly property int fontSizeCaption: root.fontPx(0.833)
    readonly property int fontSizeSmall: root.fontPx(0.917)
    readonly property int fontSizeBody: root.fontPx(1.0)
    readonly property int fontSizeSubtitle: root.fontPx(1.083)
    readonly property int fontSizeTitle: root.fontPx(1.167)
    readonly property int fontSizeHeading: root.fontPx(1.333)
    readonly property int fontSizeDisplay: root.fontPx(2.0)

    // The family is left as the fontconfig alias so `omarchy font set` keeps
    // working; main.cpp puts it on the application font and everything
    // inherits it.
    readonly property string monoFamily: "monospace"

    // ---------------------------------------------------------- spacing
    // Omarchy's scale in px, multiplied by the theme's spacing scale and by
    // the font scale so a larger base size opens the layout up with it.
    readonly property real scale: theme.spacingScale * (root.fontBase / 12)
    function px(value) { return Math.max(1, Math.round(value * root.scale)) }

    readonly property int xxs: root.px(2)
    readonly property int xs: root.px(3)
    readonly property int sm: root.px(4)
    readonly property int md: root.px(6)
    readonly property int lg: root.px(8)
    readonly property int xl: root.px(10)
    readonly property int xxl: root.px(12)
    readonly property int xxxl: root.px(14)
    readonly property int huge: root.px(18)

    // Component metrics, again the shell's own numbers.
    readonly property int controlHeight: root.px(28)
    readonly property int controlPaddingX: root.px(10)
    readonly property int controlPaddingY: root.px(6)
    readonly property int inputPaddingY: root.px(7)
    readonly property int rowGap: root.px(8)
    readonly property int rowPaddingX: root.px(12)
    readonly property int labelGap: root.px(4)
    readonly property int panelGap: root.px(14)
    readonly property int panelPadding: root.px(18)
    readonly property int popupPadding: root.px(14)
    // A mapping row carries two lines, so it is taller than the shell's
    // single-line popup row.
    readonly property int rowHeight: root.px(44)

    // Kept so existing call sites keep working; one unit is the shell's `lg`.
    function space(multiplier) { return root.px(8 * multiplier) }

    // ---------------------------------------------------------- geometry
    // Mirrored from Hyprland: a desktop with square windows gets a square app.
    readonly property int radius: theme.cornerRadius
    readonly property int radiusSmall: theme.cornerRadius > 0
        ? Math.max(1, Math.round(theme.cornerRadius * 0.6))
        : 0
    readonly property int gapsOut: theme.gapsOut

    // ---------------------------------------------------------- motion
    // Short and quiet: movement explains where something came from, it is
    // not decoration. Everything reads these, so one place sets the pace.
    readonly property int durationFast: 110
    readonly property int durationMedium: 180
    readonly property int durationSlow: 260
    readonly property int easing: Easing.OutCubic

    // ---------------------------------------------------------- icons
    // Material Design glyphs from the Nerd Font Omarchy ships, so icons follow
    // the interface font and need no image assets.
    readonly property var icon: ({
        keyboard: "\u{F030C}",
        shortcuts: "\u{F140B}",
        advanced: "\u{F062E}",
        settings: "\u{F0493}",
        bluetooth: "\u{F00AF}",
        usb: "\u{F0553}",
        internal: "\u{F0322}",
        layers: "\u{F0328}",
        check: "\u{F012C}",
        close: "\u{F0156}",
        minus: "\u{F0374}",
        plus: "\u{F0415}",
        edit: "\u{F03EB}",
        trash: "\u{F0A7A}",
        undo: "\u{F054C}",
        backup: "\u{F006F}",
        recipe: "\u{F0068}",
        arrow: "\u{F0054}",
        warning: "\u{F0026}",
    })

    // ---------------------------------------------------------- helpers
    function accented(alpha) {
        return Qt.rgba(root.accent.r, root.accent.g, root.accent.b, alpha)
    }
    function tinted(colour, alpha) {
        return Qt.rgba(colour.r, colour.g, colour.b, alpha)
    }
}
