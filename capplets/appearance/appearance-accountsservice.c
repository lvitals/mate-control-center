/*
 * Copyright (C) 2026 The MATE developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "appearance.h"
#include "appearance-accountsservice.h"

#include <gio/gio.h>

#define ACCOUNTS_DBUS_NAME              "org.freedesktop.Accounts"
#define ACCOUNTS_DBUS_PATH              "/org/freedesktop/Accounts"
#define ACCOUNTS_DBUS_INTERFACE         "org.freedesktop.Accounts"
#define ACCOUNTS_FIND_USER_BY_NAME      "FindUserByName"

#define DBUS_PROPERTIES_INTERFACE       "org.freedesktop.DBus.Properties"
#define DBUS_PROPERTIES_SET             "Set"

#define LIGHTDM_ACCOUNTS_INTERFACE      "org.freedesktop.DisplayManager.AccountsService"
#define LIGHTDM_BACKGROUND_FILE         "BackgroundFile"

void
appearance_accountsservice_set_background_file (const gchar *filename)
{
#if HAVE_ACCOUNTSSERVICE
  GDBusConnection *connection;
  GVariant *result;
  GError *error = NULL;
  gchar *user_path = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (connection == NULL)
  {
    g_debug ("Could not connect to the system bus to update AccountsService background: %s",
             error->message);
    g_error_free (error);
    return;
  }

  result = g_dbus_connection_call_sync (connection,
                                        ACCOUNTS_DBUS_NAME,
                                        ACCOUNTS_DBUS_PATH,
                                        ACCOUNTS_DBUS_INTERFACE,
                                        ACCOUNTS_FIND_USER_BY_NAME,
                                        g_variant_new ("(s)", g_get_user_name ()),
                                        G_VARIANT_TYPE ("(o)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        1000,
                                        NULL,
                                        &error);

  if (result == NULL)
  {
    g_debug ("Could not find current user in AccountsService: %s", error->message);
    g_error_free (error);
    g_object_unref (connection);
    return;
  }

  g_variant_get (result, "(o)", &user_path);
  g_variant_unref (result);

  result = g_dbus_connection_call_sync (connection,
                                        ACCOUNTS_DBUS_NAME,
                                        user_path,
                                        DBUS_PROPERTIES_INTERFACE,
                                        DBUS_PROPERTIES_SET,
                                        g_variant_new ("(ssv)",
                                                       LIGHTDM_ACCOUNTS_INTERFACE,
                                                       LIGHTDM_BACKGROUND_FILE,
                                                       g_variant_new_string (filename ? filename : "")),
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        1000,
                                        NULL,
                                        &error);

  if (result == NULL)
  {
    g_debug ("Could not update LightDM AccountsService background for %s: %s",
             g_get_user_name (),
             error->message);
    g_error_free (error);
  }
  else
  {
    g_variant_unref (result);
  }

  g_free (user_path);
  g_object_unref (connection);
#else
  (void) filename;
#endif
}
