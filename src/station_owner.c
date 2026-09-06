// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/station_owner.h"

#include <string.h>

void flex1500_station_owner_init(flex1500_station_owner *owner)
{
    if (owner != NULL) memset(owner, 0, sizeof(*owner));
}

bool flex1500_station_owner_matches(const flex1500_station_owner *owner,
                                    uint64_t lease)
{
    return owner != NULL && owner->held && lease != 0 && owner->lease == lease;
}

flex1500_station_owner_result flex1500_station_owner_acquire(
    flex1500_station_owner *owner, uint64_t lease, uint64_t now_ms)
{
    if (owner == NULL || lease == 0) return FLEX1500_STATION_OWNER_INVALID;
    if (owner->held) return FLEX1500_STATION_OWNER_BUSY;
    owner->held = true;
    owner->lease = lease;
    owner->renewed_ms = now_ms;
    return FLEX1500_STATION_OWNER_OK;
}

flex1500_station_owner_result flex1500_station_owner_keepalive(
    flex1500_station_owner *owner, uint64_t lease, uint64_t now_ms)
{
    if (!flex1500_station_owner_matches(owner, lease))
        return FLEX1500_STATION_OWNER_STALE;
    owner->renewed_ms = now_ms;
    return FLEX1500_STATION_OWNER_OK;
}

flex1500_station_owner_result flex1500_station_owner_release(
    flex1500_station_owner *owner, uint64_t lease)
{
    if (!flex1500_station_owner_matches(owner, lease))
        return FLEX1500_STATION_OWNER_STALE;
    flex1500_station_owner_init(owner);
    return FLEX1500_STATION_OWNER_OK;
}

flex1500_station_owner_result flex1500_station_owner_tick(
    flex1500_station_owner *owner, uint64_t now_ms)
{
    if (owner == NULL || !owner->held) return FLEX1500_STATION_OWNER_OK;
    if (now_ms - owner->renewed_ms < FLEX1500_STATION_OWNER_LEASE_MS)
        return FLEX1500_STATION_OWNER_OK;
    flex1500_station_owner_init(owner);
    return FLEX1500_STATION_OWNER_EXPIRED;
}
