/****************************************************************************************
** Copyright (c) 2019 - 2023 Jolla Ltd.
**
** All rights reserved.
**
** This file is part of Sailfish Device Encryption package.
**
** You may use this file under the terms of BSD license as follows:
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
**    list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
****************************************************************************************/

#ifndef __ENCRYPT_H
#define __ENCRYPT_H

#include "erase.h"

typedef enum _encryption_state {
    ENCRYPTION_NOT_STARTED,
    ENCRYPTION_IN_PREPARATION,
    ENCRYPTION_ERASURE_IN_PROGRESS,
    ENCRYPTION_IN_PROGRESS,
    ENCRYPTION_NEEDS_RESCAN,
    ENCRYPTION_RESCAN_FINISHED,
    ENCRYPTION_FINISHED,
    ENCRYPTION_FAILED,
} encryption_state;

typedef void (*encryption_status_changed)(encryption_state);

void init_encryption_service(encryption_status_changed);
gboolean start_to_encrypt(
        gchar *passphrase,
        gboolean passphrase_is_temporary,
        erase_t erase);
encryption_state get_encryption_status(void);

#endif // __ENCRYPT_H
