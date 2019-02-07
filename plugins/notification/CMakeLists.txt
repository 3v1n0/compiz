find_package (Compiz REQUIRED)

include (CompizPlugin)

compiz_plugin (notification PKGDEPS libnotify)
pkg_check_modules (LIBNOTIFY libnotify>=0.7.0)
