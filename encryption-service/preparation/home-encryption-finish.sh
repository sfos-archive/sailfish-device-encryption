#!/bin/sh

# If encryption finished, remove marker file, otherwise exit
[ -s /etc/crypttab ] && rm -f /var/lib/sailfish-device-encryption/encrypt-home

usermod --home /home/nemo nemo

# Move /home back to /home partition if it is mounted
if $(mount | grep -q " on /home type" && [ ! -e /home/nemo ]); then
    # /home was wiped, copy stuff back
    mv --target-directory=/home/ /tmp/home/.[!.]* /tmp/home/*
fi

# Clean up
rm -rf /tmp/home/
