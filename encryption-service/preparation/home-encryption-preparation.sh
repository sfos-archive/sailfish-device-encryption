#!/bin/sh
usermod --move-home --home /tmp/nemo nemo || exit 4
systemctl stop home.mount
