include $(top_srcdir)/Makefile.decl

noinst_LTLIBRARIES = \
	libegg.la \
	libegg-asn1x.la \
	libegg-hex.la \
	libegg-secmem.la \
	libegg-test.la

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/build \
	-DWITH_VALGRIND

libegg_la_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(LIBGCRYPT_CFLAGS)

libegg_la_LIBADD = \
	$(GLIB_LIBS) \
	$(LIBGCRYPT_LIBS)

libegg_la_SOURCES = \
	egg-armor.c egg-armor.h \
	egg-asn1x.c egg-asn1x.h \
	egg-asn1-defs.c egg-asn1-defs.h \
	egg-buffer.c egg-buffer.h \
	egg-dh.c egg-dh.h \
	egg-dn.c egg-dn.h \
	egg-error.h \
	egg-decimal.c egg-decimal.h \
	egg-hex.c egg-hex.h \
	egg-hkdf.c egg-hkdf.h \
	egg-libgcrypt.c egg-libgcrypt.h \
	egg-oid.c egg-oid.h \
	egg-padding.c egg-padding.h \
	egg-openssl.c egg-openssl.h \
	egg-secure-memory.c egg-secure-memory.h \
	egg-symkey.c egg-symkey.h \
	egg-testing.c egg-testing.h \
	egg-timegm.c egg-timegm.h \
	pk.asn.h pkix.asn.h \
	$(NULL)

EXTRA_DIST = \
	pk.asn \
	pkix.asn \
	$(NULL)

# --------------------------------------------------------------------
# COMMON STUFF COMPILED INTO SMALLER COMPONENTS

libegg_asn1x_la_SOURCES = \
	egg-asn1x.c egg-asn1x.h \
	$(BUILT_SOURCES)

libegg_asn1x_la_CFLAGS = \
	$(GLIB_CFLAGS)

libegg_hex_la_SOURCES = \
	egg-hex.c egg-hex.h

libegg_hex_la_CFLAGS = \
	$(GLIB_CFLAGS)

libegg_hex_la_LIBADD = \
	$(GLIB_LIBS)

libegg_secmem_la_SOURCES = \
	egg-secure-memory.c egg-secure-memory.h

libegg_test_la_SOURCES = \
	egg-testing.c egg-testing.h

libegg_test_la_CFLAGS = \
	$(GLIB_CFLAGS)

libegg_test_la_LIBADD = \
	$(GLIB_LIBS)

# -------------------------------------------------------------------

SUBDIRS = . tests

check-memory:
	make -C tests check-memory

asn:
	asn1Parser -o pk.asn.h pk.asn
	asn1Parser -o pkix.asn.h pkix.asn
	sed -i 's|#include.*|/* \0 */|' pk.asn.h pkix.asn.h
