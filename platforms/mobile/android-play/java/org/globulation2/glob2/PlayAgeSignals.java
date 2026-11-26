// SPDX-License-Identifier: GPL-3.0-or-later
package org.globulation2.glob2;

import android.app.Activity;
import com.google.android.play.agesignals.AgeSignalsAccessRequest;
import com.google.android.play.agesignals.AgeSignalsManager;
import com.google.android.play.agesignals.AgeSignalsManagerFactory;
import com.google.android.play.agesignals.AgeSignalsRequest;
import com.google.android.play.agesignals.AgeSignalsResult;
import com.google.android.play.agesignals.model.AgeSignalsStatus;

/**
 * Google Play Age Signals for the age band of social features (src/app/AgeGate.h).
 * Only the Google Play build compiles this file, with the
 * com.google.android.play:age-signals dependency (platforms/mobile/android-play/);
 * the F-Droid and Amazon builds leave the whole directory out. Glob2Activity finds
 * it by name and the answer goes back through Glob2Activity.ageSignalAnswered:
 * 0 unavailable (no data here, an old Play Store, an error), 1 declined (not shared,
 * or not yet verified), 2 under 13, 3 aged 13 or over.
 */
final class PlayAgeSignals {
    private PlayAgeSignals() {}

    /** Called on the UI thread; answers exactly once. */
    static void request(Activity activity) {
        final AgeSignalsManager manager;
        try {
            manager = AgeSignalsManagerFactory.create(activity.getApplicationContext());
        } catch (RuntimeException failure) {
            Glob2Activity.ageSignalAnswered(0);
            return;
        }
        // Sharing comes first: where the law requires it Play has the player verify or a
        // parent approve; elsewhere it follows the player's sharing setting.
        manager.requestAgeSignalsAccess(AgeSignalsAccessRequest.builder().setActivity(activity).build())
            .addOnSuccessListener(access -> {
                Integer status = access.ageSignalsStatus();
                if (status == null || status != AgeSignalsStatus.SHARED) {
                    Glob2Activity.ageSignalAnswered(status != null && (status == AgeSignalsStatus.NOT_SHARED
                        || status == AgeSignalsStatus.VERIFICATION_REQUIRED) ? 1 : 0);
                    return;
                }
                manager.checkAgeSignals(AgeSignalsRequest.builder().build())
                    .addOnSuccessListener(result -> Glob2Activity.ageSignalAnswered(code(result)))
                    .addOnFailureListener(failure -> Glob2Activity.ageSignalAnswered(0));
            })
            .addOnFailureListener(failure -> Glob2Activity.ageSignalAnswered(0));
    }

    /** Play's ranges (0-12, 13-15, 16-17, 18+) against the age gate of 13. */
    static int code(AgeSignalsResult result) {
        Integer lower = result.ageLower(), upper = result.ageUpper();
        if (upper != null && upper < 13) return 2;
        if (lower != null && lower >= 13) return 3;
        return lower == null && upper == null ? 1 : 0;
    }
}
