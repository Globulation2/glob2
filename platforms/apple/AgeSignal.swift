// SPDX-License-Identifier: GPL-3.0-or-later
//
// Apple's Declared Age Range (iOS, iPadOS and macOS 26+), the platform age
// signal of the App Store builds (src/app/AgeGate.h). The game asks for an
// age gate of 13 and learns only which side of it the player is on. The C++
// side declares glob2_request_age_range and polls for the answer on the game
// thread. Linked by the iOS Xcode project (platforms/mobile/ios.py) and by the
// Mac App Store client (CMakeLists.txt, distribution=app_store).
//
// The callback gets 0 unavailable (an older system, an error, or no window
// yet), 1 declined (the player or a parent chose not to share), 2 under 13,
// 3 aged 13 or over. It is called exactly once, on any thread.

import Foundation
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

#if canImport(DeclaredAgeRange)
import DeclaredAgeRange
#endif

#if os(iOS)
@MainActor
private func presenter() -> UIViewController? {
    let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
    let window = scenes.flatMap { $0.windows }.first { $0.isKeyWindow } ?? scenes.first?.windows.first
    var controller = window?.rootViewController
    while let presented = controller?.presentedViewController {
        controller = presented
    }
    return controller
}
#elseif os(macOS)
@MainActor
private func presenter() -> NSWindow? {
    NSApplication.shared.keyWindow ?? NSApplication.shared.mainWindow ?? NSApplication.shared.windows.first
}
#endif

#if canImport(DeclaredAgeRange)
/// The answer for an age gate of 13: under 13, 13 or over, or neither bound known.
@available(iOS 26.0, macOS 26.0, *)
private func code(for response: AgeRangeService.Response) -> Int32 {
    switch response {
    case .declinedSharing:
        return 1
    case .sharing(let range):
        if let upper = range.upperBound, upper < 13 {
            return 2
        }
        if let lower = range.lowerBound, lower >= 13 {
            return 3
        }
        return 0
    @unknown default:
        return 0
    }
}
#endif

@_cdecl("glob2_request_age_range")
public func glob2RequestAgeRange(_ done: @convention(c) (Int32) -> Void) {
#if canImport(DeclaredAgeRange) && (os(iOS) || os(macOS))
    guard #available(iOS 26.0, macOS 26.0, *) else {
        done(0)
        return
    }
    Task { @MainActor in
        guard let anchor = presenter() else {
            done(0)
            return
        }
        do {
            done(code(for: try await AgeRangeService.shared.requestAgeRange(ageGates: 13, in: anchor)))
        } catch {
            done(0)
        }
    }
#else
    done(0)
#endif
}
