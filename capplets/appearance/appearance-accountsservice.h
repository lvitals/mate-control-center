/*
 * Copyright (C) 2026 The MATE developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __APPEARANCE_ACCOUNTSSERVICE_H__
#define __APPEARANCE_ACCOUNTSSERVICE_H__

#include "appearance.h"

void appearance_accountsservice_set_background_file (const gchar *filename);
void appearance_accountsservice_sync_background (AppearanceData *data);
void appearance_accountsservice_sync_appearance (AppearanceData *data);

#endif /* __APPEARANCE_ACCOUNTSSERVICE_H__ */
