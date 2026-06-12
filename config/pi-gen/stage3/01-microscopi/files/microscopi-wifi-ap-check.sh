#!/bin/bash
# Bring up the Microscopi AP if wlan0 is not associated after 45 seconds.
# Runs once at boot via microscopi-wifi-ap.service.
sleep 45
if ! iw wlan0 link 2>/dev/null | grep -q "Connected to"; then
    nmcli con up microscopi-ap
    logger -t microscopi-ap "AP started — SSID=Microscopi, no-password, IP=192.168.42.1"
    logger -t microscopi-ap "Web UI: http://microscopi.local:8080/ or http://192.168.42.1:8080/"
fi
