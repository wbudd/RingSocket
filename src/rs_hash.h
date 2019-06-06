// SPDX-License-Identifier: MIT
// Copyright © 2019 William Budd

#pragma once

#include <ringsocket.h>

rs_ret init_hash_state(
    void
);

rs_ret get_websocket_key_hash(
    char const * wskey_22str,
    char * dst_27str
);