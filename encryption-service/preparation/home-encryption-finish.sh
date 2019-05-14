#!/bin/sh
mount | grep -q " on /home type" || exit 4
usermod --move-home --home /home/nemo nemo
add-oneshot --user --late preload-ambience
rm -f /var/lib/sailfish-device-encryption/encrypt-home
