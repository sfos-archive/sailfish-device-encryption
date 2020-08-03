#!/bin/sh
echo Start home-encryption-copy.sh

CONF_FILE="/var/lib/sailfish-device-encryption/home_copy.conf"
COPY_MARKER_FILE="/tmp/.sailfish_copy_done"

if  ! [ -f "$CONF_FILE" ]; then
    echo "No device given"
    echo 1 > $COPY_MARKER_FILE
    exit 1
fi

DEV=$(cat $CONF_FILE)
MNTPNT=$(echo -e $(lsblk -n -o MOUNTPOINT "$DEV"))
EXIT_STATUS=0

if [ ! -w "$MNTPNT" ] || ! echo "$MNTPNT" | grep -qE "^/run/media/"; then
    echo "${MNTPNT} incorrect, home can't be copied"
    echo 1 > $COPY_MARKER_FILE
    exit 1
elif [ -d "$MNTPNT/tmp/home/" ]; then
    rm -rf "$MNTPNT"/tmp/home/
fi

WRITELOCATION=$MNTPNT/tmp
mkdir -p "$WRITELOCATION"/home

SPACE_NEEDED=$(du -sck /home/.[!.]* /home/* | grep -E $'\ttotal$' | cut -d$'\t' -f1)
SPACE_AVAILABLE=$(df -k "$WRITELOCATION" | grep '[0-9]%' | tr -s " " | cut -d' ' -f4)

if [ "$SPACE_NEEDED" -gt "$SPACE_AVAILABLE" ]; then
    echo "Not enough space!"
    EXIT_STATUS=1
fi

if [ "$EXIT_STATUS" -eq "0" ]; then
    echo "Copying user data to $WRITELOCATION"
    for dir in /home/.[!.]* /home/*; do
        if [ $(basename "$dir") != "lost+found" ]; then
            echo "copy $dir to $WRITELOCATION${dir}"
            if ! cp --archive "$dir" "$WRITELOCATION${dir}"; then
                echo "cp failed for ${dir} : $?"
                EXIT_STATUS=1
            fi
        fi
    done
    echo Home copied, exit status: $EXIT_STATUS
fi

echo $EXIT_STATUS > $COPY_MARKER_FILE

exit $EXIT_STATUS
