/* Copied from geo-tp/Cardputer-Game-Station-Emulators (src/vfs/vfs_xip.h), commit 74b4776,
   unmodified apart from this notice.
   SPDX-License-Identifier: MIT
   Copyright (c) geo-tp and contributors */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void vfs_xip_register(void);
void vfs_xip_unregister(void);

#ifdef __cplusplus
}
#endif
