#!/bin/sh

HOME_WIPED=""
USERS=$(getent group users | cut -d : -f 4 | tr , " ")
for user in $USERS; do
    HOME_DIR=$(getent passwd $user | cut -d : -f 6)
    NEW_HOME=${HOME_DIR##/tmp}
    usermod --home $NEW_HOME $user
    if [ ! -e $NEW_HOME ]; then
        HOME_WIPED="1"
    fi
done

# Move /home back to /home partition if it is mounted
if $(mount | grep -q " on /home type") && [ "$HOME_WIPED" != "" ]; then
    # /home was wiped, copy stuff back
    mv /tmp/home/.[!.]* /tmp/home/* /home/
fi

# Clean up
rm -rf /tmp/home/

# If encryption finished, remove marker file
[ -s /etc/crypttab ] && rm -f /var/lib/sailfish-device-encryption/encrypt-home
