#!/bin/sh
echo Start restoration

COPY_MARKER_FILE="/tmp/.sailfish_copy_done"
COPY_SUCCESS=false

# ---- RESTORATION FROM SD CARD ------
CONF_FILE="/var/lib/sailfish-device-encryption/home_copy.conf"
if [ -f $CONF_FILE ] && grep -q "^/dev/" $CONF_FILE ; then
    SD_DEVICE=$(cat $CONF_FILE)
    COPY_SUCCESS=true
    if ! lsblk -n -o MOUNTPOINT $SD_DEVICE | grep -q "/run/" ; then
        udisksctl-user mount -b $SD_DEVICE
    fi
    MNTPNT=$(echo -e $(lsblk -n -o MOUNTPOINT $SD_DEVICE))
    SDHOME=$MNTPNT/tmp/home
    for dir in $SDHOME/.[!.]* $SDHOME/*; do
        echo "copy ${dir}"
        # a new quota is created for the encrypted filesystem
        if [[ ${dir#"$SDHOME"} == "/aquota.user" ]]; then
            continue
        elif ! cp -af $dir /home; then
            >&2 echo "Warning: ${dir} was not restored succesfully. Data is kept in $SDHOME"
            COPY_SUCCESS=false
        fi
    done
    if [ "$COPY_SUCCESS" = true ]; then
        rm -rf $SDHOME
        echo 0 > $COPY_MARKER_FILE
    fi
fi

# ---- SD RESTORATION FINISHED ------
rm -f $CONF_FILE
if [ "$COPY_SUCCESS" != true ]; then
    echo 1 > $COPY_MARKER_FILE
fi

sleep 10

systemctl restart user@100000
