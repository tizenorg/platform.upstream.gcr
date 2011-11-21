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
#include "config.h"

#define DEBUG_FLAG GCR_DEBUG_KEY
#include "gcr-debug.h"
#include "gcr-oids.h"
#include "gcr-subject-public-key.h"
#include "gcr-types.h"

#include "egg/egg-asn1x.h"
#include "egg/egg-asn1-defs.h"
#include "egg/egg-error.h"

#include <glib/gi18n-lib.h>

#include <gcrypt.h>

static gboolean
check_object_basics (GckAttributes *attributes,
                     gulong *klass,
                     gulong *type)
{
	g_assert (klass != NULL);
	g_assert (type != NULL);

	if (!gck_attributes_find_ulong (attributes, CKA_CLASS, klass))
		return FALSE;

	if (*klass == CKO_PUBLIC_KEY || *klass == CKO_PRIVATE_KEY)
		return gck_attributes_find_ulong (attributes, CKA_KEY_TYPE, type);

	else if (*klass == CKO_CERTIFICATE)
		return gck_attributes_find_ulong (attributes, CKA_CERTIFICATE_TYPE, type);

	*type = GCK_INVALID;
	return FALSE;
}

static gboolean
load_object_basics (GckObject *object,
                    GckAttributes *attributes,
                    GCancellable *cancellable,
                    gulong *klass,
                    gulong *type,
                    GError **lerror)
{
	GckAttributes *attrs;
	GError *error = NULL;

	g_assert (klass != NULL);
	g_assert (type != NULL);

	if (check_object_basics (attributes, klass, type)) {
		_gcr_debug ("already loaded: class = %lu, type = %lu", *klass, *type);
		return TRUE;
	}

	attrs = gck_object_get (object, cancellable, &error,
	                        CKA_CLASS, CKA_KEY_TYPE, CKA_CERTIFICATE_TYPE, GCK_INVALID);
	if (error != NULL) {
		_gcr_debug ("couldn't load: %s", error->message);
		g_propagate_error (lerror, error);
		return FALSE;
	}

	gck_attributes_set_all (attributes, attrs);
	gck_attributes_unref (attrs);

	if (!check_object_basics (attributes, klass, type))
		return FALSE;

	_gcr_debug ("loaded: class = %lu, type = %lu", *klass, *type);
	return TRUE;
}

static gboolean
check_x509_attributes (GckAttributes *attributes)
{
	GckAttribute *value = gck_attributes_find (attributes, CKA_VALUE);
	return (value && !gck_attribute_is_invalid (value));
}

static gboolean
load_x509_attributes (GckObject *object,
                      GckAttributes *attributes,
                      GCancellable *cancellable,
                      GError **lerror)
{
	GckAttributes *attrs;
	GError *error = NULL;

	if (check_x509_attributes (attributes)) {
		_gcr_debug ("already loaded");
		return TRUE;
	}

	attrs = gck_object_get (object, cancellable, &error,
	                        CKA_VALUE, GCK_INVALID);
	if (error != NULL) {
		_gcr_debug ("couldn't load: %s", error->message);
		g_propagate_error (lerror, error);
		return FALSE;
	}

	gck_attributes_set_all (attributes, attrs);
	gck_attributes_unref (attrs);

	return check_x509_attributes (attributes);
}

static gboolean
check_rsa_attributes (GckAttributes *attributes)
{
	GckAttribute *modulus;
	GckAttribute *exponent;

	modulus = gck_attributes_find (attributes, CKA_MODULUS);
	exponent = gck_attributes_find (attributes, CKA_PUBLIC_EXPONENT);

	return (modulus && !gck_attribute_is_invalid (modulus) &&
	        exponent && !gck_attribute_is_invalid (exponent));
}

static gboolean
load_rsa_attributes (GckObject *object,
                     GckAttributes *attributes,
                     GCancellable *cancellable,
                     GError **lerror)
{
	GckAttributes *attrs;
	GError *error = NULL;

	if (check_rsa_attributes (attributes)) {
		_gcr_debug ("rsa attributes already loaded");
		return TRUE;
	}

	attrs = gck_object_get (object, cancellable, &error,
	                        CKA_MODULUS, CKA_PUBLIC_EXPONENT, GCK_INVALID);
	if (error != NULL) {
		_gcr_debug ("couldn't load rsa attributes: %s", error->message);
		g_propagate_error (lerror, error);
		return FALSE;
	}

	gck_attributes_set_all (attributes, attrs);
	gck_attributes_unref (attrs);

	return check_rsa_attributes (attributes);
}

static GckObject *
lookup_public_key (GckObject *object,
                   GCancellable *cancellable,
                   GError **lerror)
{
	GckAttributes *match;
	GError *error = NULL;
	GckSession *session;
	GckObject *result;
	GList *objects;
	guchar *id;
	gsize n_id;

	id = gck_object_get_data (object, CKA_ID, cancellable, &n_id, &error);
	if (error != NULL) {
		_gcr_debug ("couldn't load private key id: %s", error->message);
		g_propagate_error (lerror, error);
		return NULL;
	}

	match = gck_attributes_new ();
	gck_attributes_add_ulong (match, CKA_CLASS, CKO_PUBLIC_KEY);
	gck_attributes_add_data (match, CKA_ID, id, n_id);
	session = gck_object_get_session (object);
	g_free (id);

	objects = gck_session_find_objects (session, match, cancellable, &error);

	gck_attributes_unref (match);
	g_object_unref (session);

	if (error != NULL) {
		_gcr_debug ("couldn't lookup public key: %s", error->message);
		g_propagate_error (lerror, error);
		return NULL;
	}

	if (!objects)
		return NULL;

	result = g_object_ref (objects->data);
	gck_list_unref_free (objects);

	return result;
}

static gboolean
check_dsa_attributes (GckAttributes *attributes)
{
	GckAttribute *prime;
	GckAttribute *subprime;
	GckAttribute *base;
	GckAttribute *value;

	prime = gck_attributes_find (attributes, CKA_PRIME);
	subprime = gck_attributes_find (attributes, CKA_SUBPRIME);
	base = gck_attributes_find (attributes, CKA_BASE);
	value = gck_attributes_find (attributes, CKA_VALUE);

	return (prime && !gck_attribute_is_invalid (prime) &&
	        subprime && !gck_attribute_is_invalid (subprime) &&
	        base && !gck_attribute_is_invalid (base) &&
	        value && !gck_attribute_is_invalid (value));
}

static gboolean
load_dsa_attributes (GckObject *object,
                     GckAttributes *attributes,
                     GCancellable *cancellable,
                     GError **lerror)
{
	GError *error = NULL;
	GckAttributes *loaded;
	GckObject *publi;
	gulong klass;

	if (check_dsa_attributes (attributes))
		return TRUE;

	if (!gck_attributes_find_ulong (attributes, CKA_CLASS, &klass))
		g_return_val_if_reached (FALSE);

	/* If it's a private key, find the public one */
	if (klass == CKO_PRIVATE_KEY)
		publi = lookup_public_key (object, cancellable, lerror);

	else
		publi = g_object_ref (object);

	if (!publi)
		return FALSE;

	loaded = gck_object_get (publi, cancellable, &error,
	                         CKA_PRIME, CKA_SUBPRIME, CKA_BASE, CKA_VALUE,
	                         GCK_INVALID);
	g_object_unref (publi);

	if (error != NULL) {
		_gcr_debug ("couldn't load rsa attributes: %s", error->message);
		g_propagate_error (lerror, error);
		return FALSE;
	}

	/* We've made sure to load info from the public key, so change class */
	gck_attributes_set_ulong (attributes, CKA_CLASS, CKO_PUBLIC_KEY);

	gck_attributes_set_all (attributes, loaded);
	gck_attributes_unref (loaded);

	return check_dsa_attributes (attributes);
}

static gboolean
load_attributes (GckObject *object,
                 GckAttributes *attributes,
                 GCancellable *cancellable,
                 GError **lerror)
{
	gboolean ret = FALSE;
	gulong klass;
	gulong type;

	if (!load_object_basics (object, attributes, cancellable,
	                         &klass, &type, lerror))
		return FALSE;

	switch (klass) {

	case CKO_CERTIFICATE:
		switch (type) {
		case CKC_X_509:
			ret = load_x509_attributes (object, attributes, cancellable, lerror);
			break;
		default:
			_gcr_debug ("unsupported certificate type: %lu", type);
			break;
		}
		break;

	case CKO_PUBLIC_KEY:
	case CKO_PRIVATE_KEY:
		switch (type) {
		case CKK_RSA:
			ret = load_rsa_attributes (object, attributes, cancellable, lerror);
			break;
		case CKK_DSA:
			ret = load_dsa_attributes (object, attributes, cancellable, lerror);
			break;
		default:
			_gcr_debug ("unsupported key type: %lu", type);
			break;
		}
		break;

	default:
		_gcr_debug ("unsupported class: %lu", type);
		break;
	}

	if (ret == FALSE && lerror != NULL && *lerror == NULL) {
		g_set_error_literal (lerror, GCR_DATA_ERROR, GCR_ERROR_UNRECOGNIZED,
		                     _("Unrecognized or unavailable attributes for key"));
	}

	return ret;
}

static gboolean
check_attributes (GckAttributes *attributes)
{
	gulong klass;
	gulong type;

	if (!check_object_basics (attributes, &klass, &type))
		return FALSE;

	switch (klass) {

	case CKO_CERTIFICATE:
		switch (type) {
		case CKC_X_509:
			return check_x509_attributes (attributes);
		default:
			return FALSE;
		}

	case CKO_PUBLIC_KEY:
	case CKO_PRIVATE_KEY:
		switch (type) {
		case CKK_RSA:
			return check_rsa_attributes (attributes);
		case CKK_DSA:
			return check_dsa_attributes (attributes);
		default:
			return FALSE;
		}

	default:
		return FALSE;
	}
}

static GckAttributes *
lookup_attributes (GckObject *object)
{
	GckObjectAttributes *oakey;

	if (GCK_IS_OBJECT_ATTRIBUTES (object)) {
		oakey = GCK_OBJECT_ATTRIBUTES (object);
		return gck_object_attributes_get_attributes (oakey);
	}

	return NULL;
}

static void
attributes_replace_with_copy_or_new (GckAttributes **attributes)
{
	g_assert (attributes);

	if (*attributes) {
		GckAttributes *copy = gck_attributes_dup (*attributes);
		gck_attributes_unref (*attributes);
		*attributes = copy;
	} else {
		*attributes = gck_attributes_new ();
	}
}

GNode *
_gcr_subject_public_key_load (GckObject *key,
                              GCancellable *cancellable,
                              GError **error)
{
	GckAttributes *attributes;
	GNode *asn;

	g_return_val_if_fail (GCK_IS_OBJECT (key), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	attributes = lookup_attributes (key);

	if (!attributes || !check_attributes (attributes)) {
		attributes_replace_with_copy_or_new (&attributes);

		if (!load_attributes (key, attributes, cancellable, error)) {
			gck_attributes_unref (attributes);
			return NULL;
		}
	}

	asn = _gcr_subject_public_key_for_attributes (attributes);
	if (asn == NULL) {
		g_set_error_literal (error, GCK_ERROR, CKR_TEMPLATE_INCONSISTENT,
		                     _("Couldn't build public key"));
	}

	gck_attributes_unref (attributes);
	return asn;
}

typedef struct {
	GckObject *object;
	GckAttributes *attributes;
} LoadClosure;

static void
load_closure_free (gpointer data)
{
	LoadClosure *closure = data;
	g_object_unref (closure->object);
	gck_attributes_unref (closure->attributes);
	g_slice_free (LoadClosure, closure);
}

static void
thread_key_attributes (GSimpleAsyncResult *res,
                       GObject *object,
                       GCancellable *cancellable)
{
	LoadClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	if (!load_attributes (closure->object, closure->attributes, cancellable, &error))
		g_simple_async_result_take_error (res, error);
}

void
_gcr_subject_public_key_load_async (GckObject *key,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	GSimpleAsyncResult *res;
	LoadClosure *closure;

	g_return_if_fail (GCK_IS_OBJECT (key));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 _gcr_subject_public_key_load_async);

	closure = g_slice_new0 (LoadClosure);
	closure->object = g_object_ref (key);
	closure->attributes = lookup_attributes (key);
	g_simple_async_result_set_op_res_gpointer (res, closure, load_closure_free);

	if (closure->attributes && check_attributes (closure->attributes)) {
		g_simple_async_result_complete_in_idle (res);
		g_object_unref (res);
		return;
	}

	attributes_replace_with_copy_or_new (&closure->attributes);
	g_simple_async_result_run_in_thread (res, thread_key_attributes,
	                                     G_PRIORITY_DEFAULT, cancellable);
	g_object_unref (res);
}

GNode *
_gcr_subject_public_key_load_finish (GAsyncResult *result,
                                     GError **error)
{
	GSimpleAsyncResult *res;
	LoadClosure *closure;
	GNode *asn;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      _gcr_subject_public_key_load_async), NULL);

	res = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (res, error))
		return NULL;

	closure = g_simple_async_result_get_op_res_gpointer (res);
	asn = _gcr_subject_public_key_for_attributes (closure->attributes);
	if (asn == NULL) {
		g_set_error_literal (error, GCK_ERROR, CKR_TEMPLATE_INCONSISTENT,
		                     _("Couldn't build public key"));
	}

	return asn;
}

static gboolean
rsa_subject_public_key_from_attributes (GckAttributes *attrs,
                                        GNode *info_asn)
{
	GckAttribute *modulus;
	GckAttribute *exponent;
	GNode *key_asn;
	GNode *params_asn;
	EggBytes *key;
	EggBytes *params;
	EggBytes *usg;

	_gcr_oids_init ();

	modulus = gck_attributes_find (attrs, CKA_MODULUS);
	exponent = gck_attributes_find (attrs, CKA_PUBLIC_EXPONENT);
	if (modulus == NULL || exponent == NULL)
		return FALSE;

	key_asn = egg_asn1x_create (pk_asn1_tab, "RSAPublicKey");
	g_return_val_if_fail (key_asn, FALSE);

	params_asn = egg_asn1x_create (pk_asn1_tab, "RSAParameters");
	g_return_val_if_fail (params_asn, FALSE);

	usg = egg_bytes_new_with_free_func (modulus->value, modulus->length,
	                                    gck_attributes_unref,
	                                    gck_attributes_ref (attrs));
	egg_asn1x_set_integer_as_usg (egg_asn1x_node (key_asn, "modulus", NULL), usg);
	egg_bytes_unref (usg);

	usg = egg_bytes_new_with_free_func (exponent->value, exponent->length,
	                                    gck_attributes_unref,
	                                    gck_attributes_ref (attrs));
	egg_asn1x_set_integer_as_usg (egg_asn1x_node (key_asn, "publicExponent", NULL), usg);
	egg_bytes_unref (usg);

	key = egg_asn1x_encode (key_asn, NULL);
	egg_asn1x_destroy (key_asn);

	egg_asn1x_set_null (params_asn);

	params = egg_asn1x_encode (params_asn, g_realloc);
	egg_asn1x_destroy (params_asn);

	egg_asn1x_set_bits_as_raw (egg_asn1x_node (info_asn, "subjectPublicKey", NULL),
	                           key, egg_bytes_get_size (key) * 8);

	egg_asn1x_set_oid_as_quark (egg_asn1x_node (info_asn, "algorithm", "algorithm", NULL), GCR_OID_PKIX1_RSA);
	egg_asn1x_set_element_raw (egg_asn1x_node (info_asn, "algorithm", "parameters", NULL), params);

	egg_bytes_unref (key);
	egg_bytes_unref (params);
	return TRUE;
}

static gboolean
dsa_subject_public_key_from_private (GNode *key_asn,
                                     GckAttribute *ap,
                                     GckAttribute *aq,
                                     GckAttribute *ag,
                                     GckAttribute *ax)
{
	gcry_mpi_t mp, mq, mg, mx, my;
	size_t n_buffer;
	gcry_error_t gcry;
	unsigned char *buffer;

	gcry = gcry_mpi_scan (&mp, GCRYMPI_FMT_USG, ap->value, ap->length, NULL);
	g_return_val_if_fail (gcry == 0, FALSE);

	gcry = gcry_mpi_scan (&mq, GCRYMPI_FMT_USG, aq->value, aq->length, NULL);
	g_return_val_if_fail (gcry == 0, FALSE);

	gcry = gcry_mpi_scan (&mg, GCRYMPI_FMT_USG, ag->value, ag->length, NULL);
	g_return_val_if_fail (gcry == 0, FALSE);

	gcry = gcry_mpi_scan (&mx, GCRYMPI_FMT_USG, ax->value, ax->length, NULL);
	g_return_val_if_fail (gcry == 0, FALSE);

	/* Calculate the public part from the private */
	my = gcry_mpi_snew (gcry_mpi_get_nbits (mx));
	g_return_val_if_fail (my, FALSE);
	gcry_mpi_powm (my, mg, mx, mp);

	gcry = gcry_mpi_aprint (GCRYMPI_FMT_STD, &buffer, &n_buffer, my);
	g_return_val_if_fail (gcry == 0, FALSE);
	egg_asn1x_take_integer_as_raw (key_asn, egg_bytes_new_with_free_func (buffer, n_buffer,
	                                                                      gcry_free, buffer));

	gcry_mpi_release (mp);
	gcry_mpi_release (mq);
	gcry_mpi_release (mg);
	gcry_mpi_release (mx);
	gcry_mpi_release (my);

	return TRUE;
}

static gboolean
dsa_subject_public_key_from_attributes (GckAttributes *attrs,
                                        gulong klass,
                                        GNode *info_asn)
{
	GckAttribute *value, *g, *q, *p;
	GNode *key_asn, *params_asn;
	EggBytes *key;
	EggBytes *params;

	_gcr_oids_init ();

	p = gck_attributes_find (attrs, CKA_PRIME);
	q = gck_attributes_find (attrs, CKA_SUBPRIME);
	g = gck_attributes_find (attrs, CKA_BASE);
	value = gck_attributes_find (attrs, CKA_VALUE);

	if (p == NULL || q == NULL || g == NULL || value == NULL)
		return FALSE;

	key_asn = egg_asn1x_create (pk_asn1_tab, "DSAPublicPart");
	g_return_val_if_fail (key_asn, FALSE);

	params_asn = egg_asn1x_create (pk_asn1_tab, "DSAParameters");
	g_return_val_if_fail (params_asn, FALSE);

	egg_asn1x_take_integer_as_usg (egg_asn1x_node (params_asn, "p", NULL),
	                               egg_bytes_new_with_free_func (p->value, p->length,
	                                                             gck_attributes_unref,
	                                                             gck_attributes_ref (attrs)));
	egg_asn1x_take_integer_as_usg (egg_asn1x_node (params_asn, "q", NULL),
	                               egg_bytes_new_with_free_func (q->value, q->length,
	                                                             gck_attributes_unref,
	                                                             gck_attributes_ref (attrs)));
	egg_asn1x_take_integer_as_usg (egg_asn1x_node (params_asn, "g", NULL),
	                               egg_bytes_new_with_free_func (g->value, g->length,
	                                                             gck_attributes_unref,
	                                                             gck_attributes_ref (attrs)));

	/* Are these attributes for a public or private key? */
	if (klass == CKO_PRIVATE_KEY) {

		/* We need to calculate the public from the private key */
		if (!dsa_subject_public_key_from_private (key_asn, p, q, g, value))
			g_return_val_if_reached (FALSE);

	} else if (klass == CKO_PUBLIC_KEY) {
		egg_asn1x_take_integer_as_usg (key_asn,
		                               egg_bytes_new_with_free_func (value->value, value->length,
		                                                             gck_attributes_unref,
		                                                             gck_attributes_ref (attrs)));
	} else {
		g_assert_not_reached ();
	}

	key = egg_asn1x_encode (key_asn, NULL);
	egg_asn1x_destroy (key_asn);

	params = egg_asn1x_encode (params_asn, NULL);
	egg_asn1x_destroy (params_asn);

	egg_asn1x_set_bits_as_raw (egg_asn1x_node (info_asn, "subjectPublicKey", NULL),
	                           key, egg_bytes_get_size (key) * 8);
	egg_asn1x_set_element_raw (egg_asn1x_node (info_asn, "algorithm", "parameters", NULL), params);

	egg_asn1x_set_oid_as_quark (egg_asn1x_node (info_asn, "algorithm", "algorithm", NULL), GCR_OID_PKIX1_DSA);

	egg_bytes_unref (key);
	egg_bytes_unref (params);
	return TRUE;
}

static GNode *
cert_subject_public_key_from_attributes (GckAttributes *attributes)
{
	GckAttribute *attr;
	EggBytes *bytes;
	GNode *cert;
	GNode *asn;

	attr = gck_attributes_find (attributes, CKA_VALUE);
	if (attr == NULL) {
		_gcr_debug ("no value attribute for certificate");
		return NULL;
	}

	bytes = egg_bytes_new_with_free_func (attr->value, attr->length,
	                                      gck_attributes_unref,
	                                      gck_attributes_ref (attributes));
	cert = egg_asn1x_create_and_decode (pkix_asn1_tab, "Certificate", bytes);
	egg_bytes_unref (bytes);

	if (cert == NULL) {
		_gcr_debug ("couldn't parse certificate value");
		return NULL;
	}

	asn = egg_asn1x_node (cert, "tbsCertificate", "subjectPublicKeyInfo", NULL);
	g_return_val_if_fail (asn != NULL, NULL);

	/* Remove the subject public key out of the certificate */
	g_node_unlink (asn);
	egg_asn1x_destroy (cert);

	return asn;
}

GNode *
_gcr_subject_public_key_for_attributes (GckAttributes *attributes)
{
	gboolean ret = FALSE;
	gulong key_type;
	gulong klass;
	GNode *asn = NULL;

	if (!gck_attributes_find_ulong (attributes, CKA_CLASS, &klass)) {
		_gcr_debug ("no class in attributes");
		return NULL;
	}

	if (klass == CKO_CERTIFICATE) {
		return cert_subject_public_key_from_attributes (attributes);

	} else if (klass == CKO_PUBLIC_KEY || klass == CKO_PRIVATE_KEY) {
		if (!gck_attributes_find_ulong (attributes, CKA_KEY_TYPE, &key_type)) {
			_gcr_debug ("no key type in attributes");
			return NULL;
		}

		asn = egg_asn1x_create (pkix_asn1_tab, "SubjectPublicKeyInfo");
		g_return_val_if_fail (asn, NULL);

		if (key_type == CKK_RSA) {
			ret = rsa_subject_public_key_from_attributes (attributes, asn);

		} else if (key_type == CKK_DSA) {
			ret = dsa_subject_public_key_from_attributes (attributes, klass, asn);

		} else {
			_gcr_debug ("unsupported key type: %lu", key_type);
			ret = FALSE;
		}


		if (ret == FALSE) {
			egg_asn1x_destroy (asn);
			asn = NULL;
		}
	}

	return asn;
}

static guint
calculate_rsa_key_size (EggBytes *data)
{
	GNode *asn;
	EggBytes *content;
	guint key_size;

	asn = egg_asn1x_create_and_decode (pk_asn1_tab, "RSAPublicKey", data);
	g_return_val_if_fail (asn, 0);

	content = egg_asn1x_get_raw_value (egg_asn1x_node (asn, "modulus", NULL));
	if (!content)
		g_return_val_if_reached (0);

	egg_asn1x_destroy (asn);

	/* Removes the complement */
	key_size = (egg_bytes_get_size (content) / 2) * 2 * 8;

	egg_bytes_unref (content);
	return key_size;
}

static guint
calculate_dsa_params_size (EggBytes *data)
{
	GNode *asn;
	EggBytes *content;
	guint key_size;

	asn = egg_asn1x_create_and_decode (pk_asn1_tab, "DSAParameters", data);
	g_return_val_if_fail (asn, 0);

	content = egg_asn1x_get_raw_value (egg_asn1x_node (asn, "p", NULL));
	if (!content)
		g_return_val_if_reached (0);

	egg_asn1x_destroy (asn);

	/* Removes the complement */
	key_size = (egg_bytes_get_size (content) / 2) * 2 * 8;

	egg_bytes_unref (content);
	return key_size;
}

guint
_gcr_subject_public_key_calculate_size (GNode *subject_public_key)
{
	EggBytes *key;
	guint key_size = 0;
	guint n_bits;
	GQuark oid;

	/* Figure out the algorithm */
	oid = egg_asn1x_get_oid_as_quark (egg_asn1x_node (subject_public_key,
	                                                  "algorithm", "algorithm", NULL));
	g_return_val_if_fail (oid != 0, 0);

	/* RSA keys are stored in the main subjectPublicKey field */
	if (oid == GCR_OID_PKIX1_RSA) {
		key = egg_asn1x_get_bits_as_raw (egg_asn1x_node (subject_public_key, "subjectPublicKey", NULL), &n_bits);
		g_return_val_if_fail (key != NULL, 0);
		key_size = calculate_rsa_key_size (key);
		egg_bytes_unref (key);

	/* The DSA key size is discovered by the prime in params */
	} else if (oid == GCR_OID_PKIX1_DSA) {
		key = egg_asn1x_get_element_raw (egg_asn1x_node (subject_public_key, "algorithm", "parameters", NULL));
		key_size = calculate_dsa_params_size (key);
		egg_bytes_unref (key);

	} else {
		g_message ("unsupported key algorithm: %s", g_quark_to_string (oid));
	}

	return key_size;
}
