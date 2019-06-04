#!/bin/sh
# udisks fails if crypttab doesn't exist
if [ ! -e /etc/crypttab ]; then
    touch /etc/crypttab
    chmod 600 /etc/crypttab
fi

usermod --move-home --home /tmp/nemo nemo
systemctl stop home.mount
