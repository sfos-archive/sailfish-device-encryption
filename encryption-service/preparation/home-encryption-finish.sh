#!/bin/sh

# If encryption finished, remove marker file, otherwise exit
[ -s /etc/crypttab ] || exit 4
rm -f /var/lib/sailfish-device-encryption/encrypt-home

# Move /home back to /home partition if it is mounted
if $(mount | grep -q " on /home type"); then
    usermod --move-home --home /home/nemo nemo
    mv --target-directory=/home/ /tmp/home/.[!.]* /tmp/home/*
    add-oneshot --user --late preload-ambience
else
    usermod --home /home/nemo nemo
    exit 5
fi
