// SPDX-License-Identifier: GPL-3.0-or-later
//
// Apple's Declared Age Range (iOS/iPadOS 26+), the platform age signal of the
// iOS build (src/app/AgeGate.h). The game asks for an age gate of 13 and
// learns only which side of it the player is on. The C++ side declares
// glob2_request_age_range and polls for the answer on the game thread.
//
// The callback gets 0 unavailable (an older system, an error, or no window
// yet), 1 declined (the player or a parent chose not to share), 2 under 13,
// 3 aged 13 or over. It is called exactly once, on any thread.

import Foundation
import UIKit

#if canImport(DeclaredAgeRange)
import DeclaredAgeRange
#endif

private func topViewController() -> UIViewController? {
    let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
    let window = scenes.flatMap { $0.windows }.first { $0.isKeyWindow } ?? scenes.first?.windows.first
    var controller = window?.rootViewController
    while let presented = controller?.presentedViewController {
        controller = presented
    }
    return controller
}

@_cdecl("glob2_request_age_range")
public func glob2RequestAgeRange(_ done: @convention(c) (Int32) -> Void) {
#if canImport(DeclaredAgeRange)
    guard #available(iOS 26.0, *) else {
        done(0)
        return
    }
    Task { @MainActor in
        guard let controller = topViewController() else {
            done(0)
            return
        }
        do {
            switch try await AgeRangeService.shared.requestAgeRange(ageGates: 13, in: controller) {
            case .declinedSharing:
                done(1)
            case .sharing(let range):
                if let upper = range.upperBound, upper < 13 {
                    done(2)
                } else if let lower = range.lowerBound, lower >= 13 {
                    done(3)
                } else {
                    done(0)
                }
            @unknown default:
                done(0)
            }
        } catch {
            done(0)
        }
    }
#else
    done(0)
#endif
}
