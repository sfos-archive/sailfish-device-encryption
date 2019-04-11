#!/bin/sh
mount | grep -q " on /home type" || exit 4
usermod --move-home --home /home/nemo nemo
