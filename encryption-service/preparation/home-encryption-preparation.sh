#!/bin/sh
usermod --move-home --home /tmp/nemo nemo
systemctl stop home.mount
ln -s /tmp/nemo /home/nemo
mkdir -p -m 755 /tmp/zypp-cache
ln -s /tmp/zypp-cache /home/.zypp-cache
