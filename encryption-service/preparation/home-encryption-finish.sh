#!/bin/sh
rm -f /var/lib/sailfish-device-encryption/encrypt-home
mount | grep -q " on /home type" || exit 4
usermod --move-home --home /home/nemo nemo
mv --target-directory=/home/ /tmp/home/.[!.]* /tmp/home/*
add-oneshot --user --late preload-ambience
