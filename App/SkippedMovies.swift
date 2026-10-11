import Foundation

/// Patch videos skipped when the corresponding setting is on.
///
/// These are credits reels inserted by localization groups after the publisher
/// logos, not part of the original work. The list is maintained here rather than
/// exposed as an editable field: a wrong entry silently skips real content, and
/// a name is only worth adding once it is known to be a patch video.
///
/// Matching is on the file name alone, case-insensitively, because the same
/// video appears under different directories across games. Skipping reports
/// normal end-of-playback to the runtime, so a script waiting on the movie
/// continues instead of hanging.
enum SkippedMovies {
    /// Add an entry only when it is confirmed to be a translation-patch video
    /// and not something a game uses for its own content.
    static let names = [
        "signature.wmv"
    ]

    /// The newline-separated form the runtime expects, or `nil` when skipping is
    /// off, which clears the runtime's list.
    static func runtimeList(enabled: Bool) -> String? {
        guard enabled, !names.isEmpty else { return nil }
        return names.joined(separator: "\n")
    }
}
