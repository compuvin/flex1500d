// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/station_owner.h"
#include "test_assert.h"

int main(void)
{
    flex1500_station_owner owner;
    flex1500_station_owner_init(&owner);
    CHECK(flex1500_station_owner_acquire(&owner, 41, 1000) ==
          FLEX1500_STATION_OWNER_OK);
    CHECK(flex1500_station_owner_acquire(&owner, 42, 1001) ==
          FLEX1500_STATION_OWNER_BUSY);
    CHECK(flex1500_station_owner_keepalive(&owner, 42, 2000) ==
          FLEX1500_STATION_OWNER_STALE);
    CHECK(flex1500_station_owner_keepalive(&owner, 41, 2000) ==
          FLEX1500_STATION_OWNER_OK);
    CHECK(flex1500_station_owner_tick(&owner, 16999) ==
          FLEX1500_STATION_OWNER_OK);
    CHECK(flex1500_station_owner_tick(&owner, 17000) ==
          FLEX1500_STATION_OWNER_EXPIRED);
    CHECK(!owner.held);
    CHECK(flex1500_station_owner_acquire(&owner, 42, 18000) ==
          FLEX1500_STATION_OWNER_OK);
    CHECK(flex1500_station_owner_release(&owner, 41) ==
          FLEX1500_STATION_OWNER_STALE);
    CHECK(flex1500_station_owner_release(&owner, 42) ==
          FLEX1500_STATION_OWNER_OK);
    return 0;
}
