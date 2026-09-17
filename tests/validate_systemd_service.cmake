# SPDX-License-Identifier: GPL-3.0-only

file(READ "${SERVICE_FILE}" service)

foreach(required
        "ExecStart=/usr/bin/flex1500d --config /etc/flex1500d/flex1500d.conf"
        "Restart=on-failure"
        "TimeoutStopSec=15s"
        "WantedBy=multi-user.target")
    string(FIND "${service}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "systemd unit is missing: ${required}")
    endif()
endforeach()

string(FIND "${service}" "--initialize-radio-and-enable-transmit" transmit_mode)
if(NOT transmit_mode EQUAL -1)
    message(FATAL_ERROR "systemd unit must not enable transmit on its command line")
endif()
