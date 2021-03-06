/*
 * gnome-keyring
 *
 * Copyright (C) 2011 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Stef Walter <stefw@collabora.co.uk>
 */

#if !defined (__GCR_INSIDE_HEADER__) && !defined (GCR_COMPILATION)
#error "Only <gcr/gcr.h> or <gcr/gcr-base.h> can be included directly."
#endif

#ifndef GCR_SUBJECT_PUBLIC_KEY_H
#define GCR_SUBJECT_PUBLIC_KEY_H

#include <glib.h>

#include <gck/gck.h>

G_BEGIN_DECLS

GNode *        _gcr_subject_public_key_for_attributes   (GckAttributes *attributes);

GNode *        _gcr_subject_public_key_load             (GckObject *key,
                                                         GCancellable *cancellable,
                                                         GError **error);

void           _gcr_subject_public_key_load_async       (GckObject *key,
                                                         GCancellable *cancellable,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

GNode *        _gcr_subject_public_key_load_finish      (GAsyncResult *result,
                                                         GError **error);

guint          _gcr_subject_public_key_calculate_size   (GNode *subject_public_key);

G_END_DECLS

#endif /* GCR_CERTIFICATE_H */
