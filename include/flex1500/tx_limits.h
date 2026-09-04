// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TX_LIMITS_H
#define FLEX1500_TX_LIMITS_H

/*
 * General voice/data TX defaults to the FLEX-1500's full nominal output at
 * KB1JDX's request.  One hundred percent is also an absolute I/Q-amplitude
 * ceiling, not a promise of calibrated RF power.  The capture-matched Tune
 * carrier is a separate, deliberately full-drive path.
 */
enum {
    FLEX1500_TX_MIN_DRIVE_PERCENT = 1,
    FLEX1500_TX_DEFAULT_DRIVE_PERCENT = 100,
    FLEX1500_TX_MAX_DRIVE_PERCENT = 100,
};

#endif
