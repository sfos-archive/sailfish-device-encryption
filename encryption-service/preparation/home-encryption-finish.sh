#!/bin/sh

# Set default home location back to /home partition
useradd -D -b /home

# Adjust current users, and check that home had been really wiped
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

# Move home content back to /home partition if it is mounted
if mount | grep -q " on /home type" && [ "$HOME_WIPED" != "" ]; then
    # /home was wiped, copy stuff back
    mv /tmp/home/.[!.]* /tmp/home/* /home/

    CONF_FILE="/var/lib/sailfish-device-encryption/home_copy.conf"
    if [ -f $CONF_FILE ] && grep -q "^/dev/" $CONF_FILE ; then
        DEFAULTHOME=$(getent passwd 100000 | cut -d : -f 6)
        SD_DEVICE=$(cat $CONF_FILE)
        MNTPNT=$(echo -e $(lsblk -n -o MOUNTPOINT $SD_DEVICE))
        SDHOME=$MNTPNT/tmp/home
        SDTMP=$MNTPNT/tmp
        touch ${DEFAULTHOME}/.jolla-startupwizard-done
        touch ${DEFAULTHOME}/.jolla-startupwizard-usersession-done
        for dir in $SDHOME/*/.local; do
            mv -f $dir ${dir#$SDTMP}
        done
    fi

    # set device owner's locale if it wasn't set
    if [ ! -e /home/.system/var/lib/environment/100000/locale.conf ]; then
        mkdir -p /home/.system/var/lib/environment/100000
        cp /etc/locale.conf /home/.system/var/lib/environment/100000/locale.conf
    fi
fi

# Clean up
rm -rf /tmp/home/

# If encryption finished, remove marker file
[ -s /etc/crypttab ] && rm -f /var/lib/sailfish-device-encryption/encrypt-home
