/*
 *  Authors: Rodney Dawes <dobey@ximian.com>
 *
 *  Copyright 2003-2006 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <string.h>
#include "appearance.h"
#include "mate-wp-item.h"

const gchar *wp_item_option_to_string (MateBGPlacement type)
{
  switch (type)
  {
    case MATE_BG_PLACEMENT_CENTERED:
      return "centered";
      break;
    case MATE_BG_PLACEMENT_FILL_SCREEN:
      return "stretched";
      break;
    case MATE_BG_PLACEMENT_SCALED:
      return "scaled";
      break;
    case MATE_BG_PLACEMENT_ZOOMED:
      return "zoom";
      break;
    case MATE_BG_PLACEMENT_TILED:
      return "wallpaper";
      break;
    case MATE_BG_PLACEMENT_SPANNED:
      return "spanned";
      break;
  }
  return "";
}

const gchar *wp_item_shading_to_string (MateBGColorType type)
{
  switch (type) {
    case MATE_BG_COLOR_SOLID:
      return "solid";
      break;
    case MATE_BG_COLOR_H_GRADIENT:
      return "horizontal-gradient";
      break;
    case MATE_BG_COLOR_V_GRADIENT:
      return "vertical-gradient";
      break;
  }
  return "";
}

MateBGPlacement wp_item_string_to_option (const gchar *option)
{
  if (!g_strcmp0(option, "centered"))
    return MATE_BG_PLACEMENT_CENTERED;
  else if (!g_strcmp0(option, "stretched"))
    return MATE_BG_PLACEMENT_FILL_SCREEN;
  else if (!g_strcmp0(option, "scaled"))
    return MATE_BG_PLACEMENT_SCALED;
  else if (!g_strcmp0(option, "zoom"))
    return MATE_BG_PLACEMENT_ZOOMED;
  else if (!g_strcmp0(option, "wallpaper"))
    return MATE_BG_PLACEMENT_TILED;
  else if (!g_strcmp0(option, "spanned"))
    return MATE_BG_PLACEMENT_SPANNED;
  else
    return MATE_BG_PLACEMENT_SCALED;
}

MateBGColorType wp_item_string_to_shading (const gchar *shade_type)
{
  if (!g_strcmp0(shade_type, "solid"))
    return MATE_BG_COLOR_SOLID;
  else if (!g_strcmp0(shade_type, "horizontal-gradient"))
    return MATE_BG_COLOR_H_GRADIENT;
  else if (!g_strcmp0(shade_type, "vertical-gradient"))
    return MATE_BG_COLOR_V_GRADIENT;
  else
    return MATE_BG_COLOR_SOLID;
}

static void set_bg_properties (MateWPItem *item)
{
  if (item->filename)
    mate_bg_set_filename (item->bg, item->filename);

  mate_bg_set_color (item->bg, item->shade_type, item->pcolor, item->scolor);
  mate_bg_set_placement (item->bg, item->options);
}

void mate_wp_item_ensure_mate_bg (MateWPItem *item)
{
  if (!item->bg) {
    item->bg = mate_bg_new ();

    set_bg_properties (item);
  }
}

void mate_wp_item_update (MateWPItem *item) {
  GSettings *settings;
  GdkRGBA color1 = { 0, 0, 0, 1.0 }, color2 = { 0, 0, 0, 1.0 };
  gchar *s;

  settings = g_settings_new (WP_SCHEMA);

  item->options = g_settings_get_enum (settings, WP_OPTIONS_KEY);

  item->shade_type = g_settings_get_enum (settings, WP_SHADING_KEY);

  s = g_settings_get_string (settings, WP_PCOLOR_KEY);
  if (s != NULL) {
    gdk_rgba_parse (&color1, s);
    g_free (s);
  }

  s = g_settings_get_string (settings, WP_SCOLOR_KEY);
  if (s != NULL) {
    gdk_rgba_parse (&color2, s);
    g_free (s);
  }

  g_object_unref (settings);

  if (item->pcolor != NULL)
    gdk_rgba_free (item->pcolor);

  if (item->scolor != NULL)
    gdk_rgba_free (item->scolor);

  item->pcolor = gdk_rgba_copy (&color1);
  item->scolor = gdk_rgba_copy (&color2);
}

MateWPItem * mate_wp_item_new (const gchar * filename,
				 GHashTable * wallpapers,
				 MateDesktopThumbnailFactory * thumbnails) {
  MateWPItem *item = g_new0 (MateWPItem, 1);

  item->filename = g_strdup (filename);
  item->fileinfo = mate_wp_info_new (filename, thumbnails);

  if (item->fileinfo != NULL && item->fileinfo->mime_type != NULL &&
      (g_str_has_prefix (item->fileinfo->mime_type, "image/") ||
       strcmp (item->fileinfo->mime_type, "application/xml") == 0)) {

    if (g_utf8_validate (item->fileinfo->name, -1, NULL))
      item->name = g_strdup (item->fileinfo->name);
    else
      item->name = g_filename_to_utf8 (item->fileinfo->name, -1, NULL,
				       NULL, NULL);

    mate_wp_item_update (item);
    mate_wp_item_ensure_mate_bg (item);
    mate_wp_item_update_description (item);

    g_hash_table_insert (wallpapers, item->filename, item);
  } else {
    mate_wp_item_free (item);
    item = NULL;
  }

  return item;
}

void mate_wp_item_free (MateWPItem * item) {
  if (item == NULL) {
    return;
  }

  g_free (item->name);
  g_free (item->filename);
  g_free (item->description);
  g_free (item->artist);

  if (item->pcolor != NULL)
    gdk_rgba_free (item->pcolor);

  if (item->scolor != NULL)
    gdk_rgba_free (item->scolor);

  mate_wp_info_free (item->fileinfo);
  if (item->bg)
    g_object_unref (item->bg);

  g_clear_pointer (&item->thumbnail_cache, g_hash_table_destroy);
  if (item->base_pixbuf != NULL)
    g_object_unref (item->base_pixbuf);
  gtk_tree_row_reference_free (item->rowref);

  g_free (item);
}

static guint
mate_wp_item_thumbnail_key (gint width,
                            gint height)
{
  return ((guint) width << 16) | (guint) height;
}

void
mate_wp_item_invalidate_thumbnail (MateWPItem *item)
{
  if (item == NULL)
    return;

  g_clear_pointer (&item->thumbnail_cache, g_hash_table_destroy);
}

gboolean
mate_wp_item_has_cached_thumbnail (MateWPItem *item,
                                   gint        width,
                                   gint        height)
{
  return item != NULL &&
         item->thumbnail_cache != NULL &&
         g_hash_table_contains (item->thumbnail_cache,
                                GUINT_TO_POINTER (mate_wp_item_thumbnail_key (width, height)));
}

static void
mate_wp_item_cache_thumbnail (MateWPItem *item,
                              GdkPixbuf  *pixbuf,
                              gint        width,
                              gint        height)
{
  if (item == NULL || pixbuf == NULL)
    return;

  if (item->thumbnail_cache == NULL)
    item->thumbnail_cache = g_hash_table_new_full (g_direct_hash,
                                                   g_direct_equal,
                                                   NULL,
                                                   g_object_unref);
  else if (g_hash_table_size (item->thumbnail_cache) > 4)
    g_hash_table_remove_all (item->thumbnail_cache);

  g_hash_table_replace (item->thumbnail_cache,
                        GUINT_TO_POINTER (mate_wp_item_thumbnail_key (width, height)),
                        g_object_ref (pixbuf));
}

static GdkPixbuf *
mate_wp_item_lookup_cached_thumbnail (MateWPItem *item,
                                      gint        width,
                                      gint        height)
{
  GdkPixbuf *pixbuf;

  if (!mate_wp_item_has_cached_thumbnail (item, width, height))
    return NULL;

  pixbuf = g_hash_table_lookup (item->thumbnail_cache,
                                GUINT_TO_POINTER (mate_wp_item_thumbnail_key (width, height)));

  return pixbuf ? g_object_ref (pixbuf) : NULL;
}

static GdkPixbuf *
add_slideshow_frame (GdkPixbuf *pixbuf)
{
  GdkPixbuf *sheet, *sheet2;
  GdkPixbuf *tmp;
  gint w, h;

  w = gdk_pixbuf_get_width (pixbuf);
  h = gdk_pixbuf_get_height (pixbuf);

  sheet = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, w, h);
  gdk_pixbuf_fill (sheet, 0x00000000);
  sheet2 = gdk_pixbuf_new_subpixbuf (sheet, 1, 1, w - 2, h - 2);
  gdk_pixbuf_fill (sheet2, 0xffffffff);
  g_object_unref (sheet2);

  tmp = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, w + 6, h + 6);

  gdk_pixbuf_fill (tmp, 0x00000000);
  gdk_pixbuf_composite (sheet, tmp, 6, 6, w, h, 6.0, 6.0, 1.0, 1.0, GDK_INTERP_NEAREST, 255);
  gdk_pixbuf_composite (sheet, tmp, 3, 3, w, h, 3.0, 3.0, 1.0, 1.0, GDK_INTERP_NEAREST, 255);
  gdk_pixbuf_composite (pixbuf, tmp, 0, 0, w, h, 0.0, 0.0, 1.0, 1.0, GDK_INTERP_NEAREST, 255);

  g_object_unref (sheet);

  return tmp;
}

static GdkPixbuf *
create_cover_thumbnail (GdkPixbuf *pixbuf,
                        gint       width,
                        gint       height)
{
  GdkPixbuf *scaled;
  GdkPixbuf *framed;
  gint source_width;
  gint source_height;
  gint scaled_width;
  gint scaled_height;
  gint src_x;
  gint src_y;

  source_width = gdk_pixbuf_get_width (pixbuf);
  source_height = gdk_pixbuf_get_height (pixbuf);

  if (source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0)
    return NULL;

  if (source_width * height > source_height * width) {
    scaled_height = height;
    scaled_width = (source_width * height + source_height - 1) / source_height;
  } else {
    scaled_width = width;
    scaled_height = (source_height * width + source_width - 1) / source_width;
  }

  scaled = gdk_pixbuf_scale_simple (pixbuf,
                                    scaled_width,
                                    scaled_height,
                                    GDK_INTERP_BILINEAR);
  if (scaled == NULL)
    return NULL;

  framed = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, width, height);
  gdk_pixbuf_fill (framed, 0x00000000);

  src_x = MAX ((scaled_width - width) / 2, 0);
  src_y = MAX ((scaled_height - height) / 2, 0);
  gdk_pixbuf_copy_area (scaled,
                        src_x,
                        src_y,
                        MIN (width, scaled_width),
                        MIN (height, scaled_height),
                        framed,
                        0,
                        0);
  g_object_unref (scaled);

  return framed;
}

static GdkPixbuf *
mate_wp_item_get_image_thumbnail (MateWPItem *item,
                                  MateDesktopThumbnailFactory *thumbs,
                                  gint        width,
                                  gint        height)
{
  GdkPixbuf *pixbuf = NULL;
  GdkPixbuf *framed;
  GError *error = NULL;
  gint image_width = 0;
  gint image_height = 0;

  if (item->filename == NULL || item->fileinfo == NULL ||
      item->fileinfo->mime_type == NULL ||
      !g_str_has_prefix (item->fileinfo->mime_type, "image/"))
    return NULL;

  if (item->base_pixbuf != NULL) {
    framed = create_cover_thumbnail (item->base_pixbuf, width, height);
    if (framed != NULL)
      return framed;
  }

  if (item->fileinfo->thumburi != NULL) {
    pixbuf = gdk_pixbuf_new_from_file (item->fileinfo->thumburi, NULL);
    if (pixbuf != NULL) {
      item->base_pixbuf = g_object_ref (pixbuf);
      framed = create_cover_thumbnail (pixbuf, width, height);
      g_object_unref (pixbuf);

      if (framed != NULL)
        return framed;
    }
  }

  gdk_pixbuf_get_file_info (item->filename, &image_width, &image_height);

  if (image_width > 0 && image_height > 0) {
    gint scaled_width;
    gint scaled_height;
    gint max_base_size = 512;

    if (image_width > max_base_size || image_height > max_base_size) {
      if (image_width > image_height) {
        scaled_width = max_base_size;
        scaled_height = (image_height * max_base_size) / image_width;
      } else {
        scaled_height = max_base_size;
        scaled_width = (image_width * max_base_size) / image_height;
      }
    } else {
      scaled_width = image_width;
      scaled_height = image_height;
    }

    pixbuf = gdk_pixbuf_new_from_file_at_scale (item->filename,
                                                scaled_width,
                                                scaled_height,
                                                TRUE,
                                                &error);
  } else {
    pixbuf = gdk_pixbuf_new_from_file_at_scale (item->filename,
                                                width > 512 ? width : 512,
                                                height > 512 ? height : 512,
                                                TRUE,
                                                &error);
  }

  if (pixbuf == NULL) {
    g_warning ("Could not create wallpaper thumbnail for '%s': %s",
               item->filename,
               error ? error->message : "unknown error");
    g_clear_error (&error);
    return NULL;
  }

  item->width = image_width > 0 ? image_width : gdk_pixbuf_get_width (pixbuf);
  item->height = image_height > 0 ? image_height : gdk_pixbuf_get_height (pixbuf);

  item->base_pixbuf = g_object_ref (pixbuf);

  framed = create_cover_thumbnail (pixbuf, width, height);

  if (framed != NULL && thumbs != NULL) {
    mate_desktop_thumbnail_factory_save_thumbnail (thumbs,
                                                   pixbuf,
                                                   item->filename,
                                                   item->fileinfo->mtime);
    g_free (item->fileinfo->thumburi);
    item->fileinfo->thumburi = mate_desktop_thumbnail_factory_lookup (thumbs,
                                                                      item->filename,
                                                                      item->fileinfo->mtime);
  }

  g_object_unref (pixbuf);

  return framed;
}

GdkPixbuf * mate_wp_item_get_frame_thumbnail (MateWPItem * item,
					       MateDesktopThumbnailFactory * thumbs,
                                               int width,
                                               int height,
                                               gint frame) {
  GdkPixbuf *pixbuf = NULL;

  if (frame == -1 && mate_wp_item_has_cached_thumbnail (item, width, height))
    return mate_wp_item_lookup_cached_thumbnail (item, width, height);

  if (frame == -1 && g_strcmp0 (item->filename, "(none)") != 0 &&
      item->fileinfo != NULL &&
      item->fileinfo->mime_type != NULL &&
      g_str_has_prefix (item->fileinfo->mime_type, "image/")) {
    pixbuf = mate_wp_item_get_image_thumbnail (item, thumbs, width, height);
    mate_wp_item_cache_thumbnail (item, pixbuf, width, height);
    return pixbuf;
  }

  set_bg_properties (item);

  if (frame != -1)
    pixbuf = mate_bg_create_frame_thumbnail (item->bg, thumbs, gdk_screen_get_default (), width, height, frame);
  else
    pixbuf = mate_bg_create_thumbnail (item->bg, thumbs, gdk_screen_get_default(), width, height);

  if (pixbuf && mate_bg_changes_with_time (item->bg))
    {
      GdkPixbuf *tmp;

      tmp = add_slideshow_frame (pixbuf);
      g_object_unref (pixbuf);
      pixbuf = tmp;
    }

  mate_bg_get_image_size (item->bg, thumbs, width, height, &item->width, &item->height);

  if (frame == -1)
    mate_wp_item_cache_thumbnail (item, pixbuf, width, height);

  return pixbuf;
}

GdkPixbuf * mate_wp_item_get_thumbnail (MateWPItem * item,
					 MateDesktopThumbnailFactory * thumbs,
                                         gint width,
                                         gint height) {
  return mate_wp_item_get_frame_thumbnail (item, thumbs, width, height, -1);
}

void mate_wp_item_update_description (MateWPItem * item) {
  g_free (item->description);

  if (!strcmp (item->filename, "(none)")) {
    item->description = g_strdup (item->name);
  } else {
    gchar *description;
    gchar *size;
    gchar *dirname = g_path_get_dirname (item->filename);
    gchar *artist;

    description = NULL;
    size = NULL;

    if (!item->artist || item->artist[0] == 0 || !g_strcmp0(item->artist, "(none)"))
      artist = g_strdup (_("unknown"));
    else
      artist = g_strdup (item->artist);

    if (strcmp (item->fileinfo->mime_type, "application/xml") == 0)
      {
        if (mate_bg_changes_with_time (item->bg))
          description = g_strdup (_("Slide Show"));
        else if (item->width > 0 && item->height > 0)
          description = g_strdup (_("Image"));
      }
    else
      description = g_content_type_get_description (item->fileinfo->mime_type);

    if (mate_bg_has_multiple_sizes (item->bg))
      size = g_strdup (_("multiple sizes"));
    else if (item->width > 0 && item->height > 0) {
      /* translators: x pixel(s) by y pixel(s) */
      size = g_strdup_printf (_("%d %s by %d %s"),
                              item->width,
                              ngettext ("pixel", "pixels", item->width),
                              item->height,
                              ngettext ("pixel", "pixels", item->height));
    }

    if (description && size) {
      /* translators: <b>wallpaper name</b>
       * mime type, size
       * Folder: /path/to/file
       * Artist: wallpaper author
       */
      item->description = g_markup_printf_escaped (_("<b>%s</b>\n"
                                                     "%s, %s\n"
                                                     "Folder: %s\n"
                                                     "Artist: %s"),
                                                   item->name,
                                                   description,
                                                   size,
                                                   dirname,
                                                   artist);
    } else {
      /* translators: <b>wallpaper name</b>
       * Image missing
       * Folder: /path/to/file
       * Artist: wallpaper author
       */
      item->description = g_markup_printf_escaped (_("<b>%s</b>\n"
                                                     "%s\n"
                                                     "Folder: %s\n"
                                                     "Artist: %s"),
                                                   item->name,
                                                   _("Image missing"),
                                                   dirname,
                                                   artist);
    }

    g_free (size);
    g_free (dirname);
    g_free (artist);
    g_free (description);
  }
}
