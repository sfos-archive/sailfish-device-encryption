/* Copyright (c) 2019 Jolla Ltd. */

#ifndef __ENCRYPT_H
#define __ENCRYPT_H

typedef enum _encryption_state {
    ENCRYPTION_NOT_STARTED,
    ENCRYPTION_IN_PREPARATION,
    ENCRYPTION_IN_PROGRESS,
    ENCRYPTION_NEEDS_RESCAN,
    ENCRYPTION_RESCAN_FINISHED,
    ENCRYPTION_FINISHED,
    ENCRYPTION_FAILED,
} encryption_state;

typedef void (*encryption_status_changed)(encryption_state);

void init_encryption_service(encryption_status_changed);
gboolean start_to_encrypt(gchar *passphrase);
encryption_state get_encryption_status(void);

#endif // __ENCRYPT_H
