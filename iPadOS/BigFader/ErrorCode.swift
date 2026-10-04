import Foundation

/// Every error the app shows carries a code, BF-AREA-NN, at the end of its
/// message. A code can be looked up rather than paraphrased: pasted into an
/// LLM along with iPadOS/ERRORS.md, which explains each one, or searched for
/// in the source, where each is raised in one kind of place. A new error
/// means a new case here and a new entry there, in the same change.
///
/// Guidance is not an error and gets no code. A code means something failed:
/// a file, the audio engine, or the library on disk.
enum ErrorCode: String {
    case importFile   = "BF-IMPORT-01"
    case importPicker = "BF-IMPORT-02"
    case setPicker    = "BF-SET-01"
    case setUnreadable = "BF-SET-02"
    case setWrongFormat = "BF-SET-03"
    case setTooNew    = "BF-SET-04"
    case setExport    = "BF-SET-05"
    case libraryRead  = "BF-LIB-01"
    case librarySave  = "BF-LIB-02"
    case engineStart  = "BF-AUDIO-01"
    case deckLoad     = "BF-DECK-01"

    /// The message as shown: what went wrong, then the code in brackets.
    func message(_ text: String) -> String {
        "\(text) (\(rawValue))"
    }
}
