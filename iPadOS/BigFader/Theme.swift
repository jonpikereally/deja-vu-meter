import SwiftUI

/// One place for the colours, so the fader and the buttons stay in step.
enum Theme {

    static let background = Color(red: 0.06, green: 0.06, blue: 0.07)
    static let panel = Color(red: 0.11, green: 0.11, blue: 0.13)
    static let trackWell = Color(red: 0.16, green: 0.16, blue: 0.18)
    static let hairline = Color.white.opacity(0.08)

    /// Amber, borrowed from the Deja VU ghost trace.
    static let amber = Color(red: 1.0, green: 0.68, blue: 0.16)
    static let red = Color(red: 0.95, green: 0.29, blue: 0.24)

    static let label = Color.white.opacity(0.55)
    static let labelStrong = Color.white.opacity(0.92)
}
