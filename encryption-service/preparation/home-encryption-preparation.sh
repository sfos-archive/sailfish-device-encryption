#!/bin/sh
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
    if [ $SPACE_ON_TMP -gt $(($(du -sk /home/nemo | cut -d$'\t' -f1) + $EXTRA_SPACE)) ]; then
        echo "Copying just /home/nemo"
        cp --archive /home/nemo /tmp/home/nemo
    else
        >&2 echo "Warning: Not enough space even for /home/nemo. Creating new."
        cp --archive /etc/skel /tmp/home/nemo
        chown --recursive nemo:nemo /tmp/home/nemo
        chmod 750 /tmp/home/nemo
        add-oneshot --user --late preload-ambience
    fi
fi

# Remove SUW marker files to enter it with pre-user-session mode
rm -f /tmp/home/nemo/.jolla-startupwizard*

usermod --home /tmp/home/nemo nemo
systemctl stop home.mount || true
