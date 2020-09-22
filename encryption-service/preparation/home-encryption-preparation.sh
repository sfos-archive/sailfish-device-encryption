#!/bin/sh

# Uninstall home-debug-location package before home encryption. This will move
# debug symbols back to rootfs or delete symbols if there is no space in rootfs.
if rpm -q jolla-developer-mode-home-debug-location > /dev/null; then
    echo "Uninstall jolla-developer-mode-home-debug-location before home encryption"
    rpm -e jolla-developer-mode-home-debug-location
fi

# udisks fails if crypttab doesn't exist
if [ ! -e /etc/crypttab ]; then
    touch /etc/crypttab
    chmod 600 /etc/crypttab
fi

mkdir /tmp/home

# Calculate 1/3 of /tmp to keep it free even after copying
EXTRA_SPACE=$(( $(df -k /tmp | grep -Eo '[0-9]+ +[0-9]+ +[0-9]+ +[0-9]+% +/tmp$' | cut -d' ' -f1) / 3 ))
# globs below miss files that start with two dots
SPACE_ON_TMP=$(df -k /tmp | grep -Eo '[0-9]+ +[0-9]+% +/tmp$' | cut -d' ' -f1)
# calculation includes lost+found even though it's not copied to /tmp
SPACE_NEEDED=$(du -sck /home/.[!.]* /home/* | grep -E $'\ttotal$' | cut -d$'\t' -f1)

USERS=$(getent group users | cut -d : -f 4 | tr , " ")

# Moving content from home partition to temporary location
if [ $SPACE_ON_TMP -gt $(($SPACE_NEEDED + $EXTRA_SPACE)) ]; then
    # move all stuff
    echo "Everything in /home fits to /tmp, copying all"
    for dir in /home/.[!.]* /home/*; do
        if [ "$(basename $dir)" != "lost+found" ]; then
            cp --archive "$dir" "/tmp${dir}"
        fi
    done
else
    # print warning and move only the necessary stuff
    >&2 echo "Warning: Not enough space to copy everything from /home to /tmp"
    USER_SPACE=0
    for user in $USERS; do
        USER_SPACE=$(( $USER_SPACE + $(du -sk $(getent passwd $user | cut -d : -f 6) | cut -d$'\t' -f1) ))
    done
    if [ $SPACE_ON_TMP -gt $(( $USER_SPACE + $EXTRA_SPACE )) ]; then
        echo "Copying just user directories"
        for user in $USERS; do
            cp --archive --parents $(getent passwd $user | cut -d : -f 6) /tmp/
        done
    else
        >&2 echo "Warning: Not enough space. Creating new home directories."
        for user in $USERS; do
            USER_HOME=$(getent passwd $user | cut -d : -f 6)
            NEW_HOME="/tmp${USER_HOME}"
            mkdir -p ${NEW_HOME%/*}
            cp --archive /etc/skel $NEW_HOME
            chown --recursive $(stat -c '%U:%G' $USER_HOME) $NEW_HOME
            chmod 750 $NEW_HOME
        done
        add-oneshot --all-users --late preload-ambience
        add-oneshot --all-users --late browser-update-default-data
    fi
fi

# Remove SUW marker files to enter it with pre-user-session mode
# except if this is a QA device
if [ ! -f /usr/lib/startup/qa-encrypt-device ]; then
    for user in $USERS; do
        USER_HOME=$(getent passwd $user | cut -d : -f 6)
        rm -f /tmp${USER_HOME}/.jolla-startupwizard*
    done
fi

# Create any users while encrypting to /tmp/home
useradd -D -b /tmp/home

# Adjust current users
for user in $USERS; do
    USER_HOME=$(getent passwd $user | cut -d : -f 6)
    usermod --home /tmp${USER_HOME} $user
done

systemctl stop home.mount || true
