pragma Singleton
import QtQuick

QtObject {
    // Shared with Monero Merchant frontend/site/admin/style.css.
    readonly property color background: "#070709"
    readonly property color surface: "#101015"
    readonly property color raised: "#1a1a22"
    readonly property color ink: "#f6f5fa"
    readonly property color secondary: "#9b97ad"
    readonly property color muted: "#807b94"
    readonly property color accent: "#ff6a2b"
    readonly property color accentHover: "#ff8347"
    readonly property color success: "#16d8a8"
    readonly property color warning: "#ffc23d"
    readonly property color danger: "#ff5d5d"
    readonly property color line: "#12ffffff"
    readonly property string uiFont: sans.name || "sans-serif"
    readonly property string monoFont: mono.name || "monospace"
    property FontLoader sans: FontLoader { source: "fonts/instrument-sans.ttf" }
    property FontLoader mono: FontLoader { source: "fonts/ibm-plex-mono-400.ttf" }
    property FontLoader monoBold: FontLoader { source: "fonts/ibm-plex-mono-600.ttf" }
}
