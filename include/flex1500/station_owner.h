// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_STATION_OWNER_H
#define FLEX1500_STATION_OWNER_H

#include <stdbool.h>
#include <stdint.h>

enum { FLEX1500_STATION_OWNER_LEASE_MS = 15000 };

typedef enum flex1500_station_owner_result {
    FLEX1500_STATION_OWNER_OK,
    FLEX1500_STATION_OWNER_BUSY,
    FLEX1500_STATION_OWNER_STALE,
    FLEX1500_STATION_OWNER_INVALID,
    FLEX1500_STATION_OWNER_EXPIRED,
} flex1500_station_owner_result;

typedef struct flex1500_station_owner {
    bool held;
    uint64_t lease;
    uint64_t renewed_ms;
} flex1500_station_owner;

void flex1500_station_owner_init(flex1500_station_owner *owner);
flex1500_station_owner_result flex1500_station_owner_acquire(
    flex1500_station_owner *owner, uint64_t lease, uint64_t now_ms);
flex1500_station_owner_result flex1500_station_owner_keepalive(
    flex1500_station_owner *owner, uint64_t lease, uint64_t now_ms);
flex1500_station_owner_result flex1500_station_owner_release(
    flex1500_station_owner *owner, uint64_t lease);
flex1500_station_owner_result flex1500_station_owner_tick(
    flex1500_station_owner *owner, uint64_t now_ms);
bool flex1500_station_owner_matches(const flex1500_station_owner *owner,
                                    uint64_t lease);

#endif
