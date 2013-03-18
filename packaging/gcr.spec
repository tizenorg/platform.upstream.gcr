%define baseline 3.7


Name:           gcr
Version:        3.7.92
Release:        0
Summary:        Library for Crypto UI related task
License:        LGPL-2.1+
Group:          Security/Crypto Libraries
Url:            http://www.gnome.org
Source0:        http://download.gnome.org/sources/gcr/%{baseline}/%{name}-%{version}.tar.xz
BuildRequires:  gnome-common
BuildRequires:  gpg2
BuildRequires:  gtk-doc
BuildRequires:  intltool
BuildRequires:  libgcrypt-devel >= 1.2.2
BuildRequires:  libtasn1-devel
BuildRequires:  shared-mime-info
BuildRequires:  update-desktop-files 
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(gio-unix-2.0)
BuildRequires:  pkgconfig(glib-2.0) >= 2.30.0
BuildRequires:  pkgconfig(gmodule-no-export-2.0)
BuildRequires:  pkgconfig(gobject-2.0)
BuildRequires:  pkgconfig(gobject-introspection-1.0)
BuildRequires:  pkgconfig(gthread-2.0)
BuildRequires:  pkgconfig(gtk+-3.0) >= 3.0
BuildRequires:  pkgconfig(libtasn1)
BuildRequires:  pkgconfig(p11-kit-1) >= 0.6

%description
GCR is a library for displaying certificates, and crypto UI, accessing
key stores. It also provides the viewer for crypto files on the GNOME
desktop.

GCK is a library for accessing PKCS#11 modules like smart cards, in a
(G)object oriented way.


%package viewer
Summary:        Viewer for Crypto Files
Group:          Security/Certificate Management

%description viewer
This packages provides the viewer for crypto files on the GNOME desktop.


%package data
Summary:        Data and icon set for gcr
Group:          Security/Crypto Libraries
Requires(post): glib2-tools
Requires(postun): glib2-tools

%description data
This package provides the GSettings schemas and a collection of icons
needed by libgcr.


%package prompter
Summary:        Prompt dialog for Crypto UI related task
Group:          System/Libraries

%description prompter
This package provides the prompt dialog needed by libgcr.


%package -n libgcr
Summary:        Library for Crypto UI related task
Group:          System/Libraries
Requires:       %{name}-data >= %{version}
Requires:       %{name}-prompter >= %{version}

%description -n libgcr
GCR is a library for displaying certificates, and crypto UI, accessing
key stores.


%package -n typelib-Gcr
Summary:        Library for Crypto UI related task -- Introspection bindings
Group:          System/Libraries

%description -n typelib-Gcr
GCR is a library for displaying certificates, and crypto UI, accessing
key stores.

This package provides the GObject Introspection bindings for GCR.


%package -n libgcr-devel
Summary:        Library for Crypto UI related task - Development Files
Group:          Development/Libraries
Requires:       libgcr = %{version}
Requires:       typelib-Gcr = %{version}

%description -n libgcr-devel
GCR is a library for displaying certificates, and crypto UI, accessing
key stores.


%package -n libgcr-ui
Summary:        Library for Crypto UI related task
Group:          System/Libraries
Requires:       %{name}-data >= %{version}
Requires:       %{name}-prompter >= %{version}

%description -n libgcr-ui
GCR is a library for displaying certificates, and crypto UI, accessing
key stores.ey stores.


%package -n typelib-GcrUi
Summary:        Library for Crypto UI related task -- Introspection bindings
Group:          System/Libraries

%description -n typelib-GcrUi
GCR is a library for displaying certificates, and crypto UI, accessing
key stores.

This package provides the GObject Introspection bindings for GCR.


%package -n libgcr-ui-devel
Summary:        Library for Crypto UI related task - Development Files
Group:          Development/Libraries
Requires:       libgcr-ui = %{version}
Requires:       libgcr-devel = %{version}
Requires:       typelib-Gcr = %{version}

%description -n libgcr-ui-devel
GCR is a library for displaying certificates, and crypto UI, accessing
key stores.


%package -n libgck
Summary:        GObject library to access for PKCS#11 modules
Group:          System/Libraries
Provides:       gck = %{version}

%description -n libgck
GCK is a library for accessing PKCS#11 modules like smart cards, in a
(G)object oriented way.


%package -n typelib-Gck
Summary:        GObject library to access for PKCS#11 modules -- Introspection bindings
Group:          Security/Crypto Libraries

%description -n typelib-Gck
GCK is a library for accessing PKCS#11 modules like smart cards, in a
(G)object oriented way.

This package provides the GObject Introspection bindings for GCK.


%package -n libgck-devel
Summary:        GObject library to access for PKCS#11 modules - Development Files
Group:          Development/Libraries
Requires:       libgck = %{version}
Requires:       typelib-Gck = %{version}

%description -n libgck-devel
GCK is a library for accessing PKCS#11 modules like smart cards, in a
(G)object oriented way.

%prep
%setup -q

%build

%autogen \
 --disable-gtk-doc-html

make

%install
%make_install
%tizen_update_desktop_file gcr-prompter
%tizen_update_desktop_file gcr-viewer
%find_lang %{name}

%post viewer
%desktop_database_post
%mime_database_post

%postun viewer
%desktop_database_postun
%mime_database_postun

%post data
%glib2_gsettings_schema_post
%icon_theme_cache_post

%postun data
%glib2_gsettings_schema_postun
%icon_theme_cache_postun

%post -n libgcr -p /sbin/ldconfig

%postun -n libgcr -p /sbin/ldconfig

%post -n libgck -p /sbin/ldconfig

%postun -n libgck -p /sbin/ldconfig

%post -n libgcr-ui -p /sbin/ldconfig

%postun -n libgcr-ui -p /sbin/ldconfig


%files viewer
%defattr(-,root,root)
%license COPYING
%{_bindir}/gcr-viewer
%{_datadir}/applications/gcr-viewer.desktop
%{_datadir}/mime/packages/gcr-crypto-types.xml


%files data
%defattr(-, root, root)
%{_datadir}/icons/hicolor/*/apps/*
%dir %{_datadir}/GConf
%dir %{_datadir}/GConf/gsettings
%{_datadir}/GConf/gsettings/org.gnome.crypto.pgp.convert
%{_datadir}/GConf/gsettings/org.gnome.crypto.pgp_keyservers.convert
%{_datadir}/glib-2.0/schemas/org.gnome.crypto.pgp.gschema.xml


%files prompter
%defattr(-, root, root)
%{_libexecdir}/gcr-prompter
%{_datadir}/applications/gcr-prompter.desktop
%{_datadir}/dbus-1/services/org.gnome.keyring.PrivatePrompter.service
%{_datadir}/dbus-1/services/org.gnome.keyring.SystemPrompter.service


%files -n libgcr
%defattr (-, root, root)
%license COPYING
%{_libdir}/libgcr-3.so.*
%{_libdir}/libgcr-base-3.so.*
%{_datadir}/gcr-3/


%files -n typelib-Gcr
%defattr(-,root,root)
%{_libdir}/girepository-1.0/Gcr-3.typelib


%files -n libgcr-devel
%defattr (-, root, root)
%{_libdir}/libgcr-3.so
%{_libdir}/libgcr-base-3.so
%{_libdir}/pkgconfig/gcr-3.pc
%{_libdir}/pkgconfig/gcr-base-3.pc
%{_includedir}/gcr-3/


%files -n libgck
%defattr (-, root, root)
%license COPYING
%{_libdir}/libgck-1.so.*


%files -n typelib-Gck
%defattr(-,root,root)
%{_libdir}/girepository-1.0/Gck-1.typelib


%files -n libgck-devel
%defattr (-, root, root)
%{_libdir}/libgck-1.so
%{_libdir}/pkgconfig/gck-1.pc
%{_includedir}/gck-1/
%{_datadir}/gir-1.0/Gck-1.gir
%{_datadir}/gir-1.0/Gcr-3.gir


%files -n libgcr-ui
%defattr (-, root, root)
%license COPYING
%{_libdir}/libgcr-ui-3.so.1
%{_libdir}/libgcr-ui-3.so.1.0.0
%{_datadir}/gir-1.0/GcrUi-3.gir


%files -n typelib-GcrUi
%defattr(-,root,root)
%{_libdir}/girepository-1.0/GcrUi-3.typelib


%files -n libgcr-ui-devel
%defattr (-, root, root)
%{_libdir}/libgcr-ui-3.so
%{_libdir}/pkgconfig/gcr-ui-3.pc

%lang_package -n libgcr
