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
#define MATE_ACCOUNTS_INTERFACE         "org.mate.DisplayManager.AccountsService"

static GDBusConnection *
accountsservice_get_current_user (gchar **user_path)
{
  GDBusConnection *connection;
  GVariant *result;
  GError *error = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (connection == NULL)
  {
    g_debug ("Could not connect to the system bus for AccountsService: %s",
             error->message);
    g_error_free (error);
    return NULL;
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
    return NULL;
  }

  g_variant_get (result, "(o)", user_path);
  g_variant_unref (result);

  return connection;
}

static void
accountsservice_set_property (const gchar *interface,
                              const gchar *property,
                              GVariant    *value)
{
#if HAVE_ACCOUNTSSERVICE
  GDBusConnection *connection;
  GVariant *result;
  GError *error = NULL;
  gchar *user_path = NULL;

  connection = accountsservice_get_current_user (&user_path);
  if (connection == NULL)
  {
    g_variant_unref (value);
    return;
  }

  result = g_dbus_connection_call_sync (connection,
                                        ACCOUNTS_DBUS_NAME,
                                        user_path,
                                        DBUS_PROPERTIES_INTERFACE,
                                        DBUS_PROPERTIES_SET,
                                        g_variant_new ("(ssv)",
                                                       interface,
                                                       property,
                                                       value),
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        1000,
                                        NULL,
                                        &error);

  if (result == NULL)
  {
    g_debug ("Could not update AccountsService property %s.%s for %s: %s",
             interface,
             property,
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
  (void) interface;
  (void) property;
  g_variant_unref (value);
#endif
}

static void
accountsservice_set_string (const gchar *interface,
                            const gchar *property,
                            const gchar *value)
{
  accountsservice_set_property (interface, property,
                                g_variant_new_string (value ? value : ""));
}

static void
accountsservice_set_int (const gchar *interface,
                         const gchar *property,
                         gint         value)
{
  accountsservice_set_property (interface, property, g_variant_new_int32 (value));
}

static void
accountsservice_set_double (const gchar *interface,
                            const gchar *property,
                            gdouble      value)
{
  accountsservice_set_property (interface, property, g_variant_new_double (value));
}

void
appearance_accountsservice_set_background_file (const gchar *filename)
{
  accountsservice_set_string (LIGHTDM_ACCOUNTS_INTERFACE,
                              LIGHTDM_BACKGROUND_FILE,
                              filename);
}

void
appearance_accountsservice_sync_appearance (AppearanceData *data)
{
  gchar *value;

  g_return_if_fail (data != NULL);

  if (data->interface_settings)
  {
    value = g_settings_get_string (data->interface_settings, GTK_THEME_KEY);
    accountsservice_set_string (MATE_ACCOUNTS_INTERFACE, "GtkTheme", value);
    g_free (value);

    value = g_settings_get_string (data->interface_settings, ICON_THEME_KEY);
    accountsservice_set_string (MATE_ACCOUNTS_INTERFACE, "IconTheme", value);
    g_free (value);

    value = g_settings_get_string (data->interface_settings, GTK_FONT_KEY);
    accountsservice_set_string (MATE_ACCOUNTS_INTERFACE, "FontName", value);
    g_free (value);
  }

  if (data->mouse_settings)
  {
    value = g_settings_get_string (data->mouse_settings, CURSOR_THEME_KEY);
    accountsservice_set_string (MATE_ACCOUNTS_INTERFACE, "CursorTheme", value);
    g_free (value);

    accountsservice_set_int (MATE_ACCOUNTS_INTERFACE, "CursorSize",
                             g_settings_get_int (data->mouse_settings, CURSOR_SIZE_KEY));
  }

  if (data->font_settings)
  {
    accountsservice_set_double (MATE_ACCOUNTS_INTERFACE, "XftDpi",
                                g_settings_get_double (data->font_settings, FONT_DPI_KEY));

    value = g_settings_get_string (data->font_settings, FONT_ANTIALIASING_KEY);
    accountsservice_set_string (MATE_ACCOUNTS_INTERFACE, "XftAntialias", value);
    g_free (value);

    value = g_settings_get_string (data->font_settings, FONT_HINTING_KEY);
    accountsservice_set_string (MATE_ACCOUNTS_INTERFACE, "XftHintStyle", value);
    g_free (value);

    value = g_settings_get_string (data->font_settings, FONT_RGBA_ORDER_KEY);
    accountsservice_set_string (MATE_ACCOUNTS_INTERFACE, "XftRgba", value);
    g_free (value);
  }
}
