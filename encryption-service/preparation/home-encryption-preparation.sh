#!/bin/sh
usermod --move-home --home /tmp/nemo nemo
systemctl stop home.mount
