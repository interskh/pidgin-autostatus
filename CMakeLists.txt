cmake_minimum_required (VERSION 2.6)

project(pidgin-autostatus)

add_definitions(-DCMAKE_INSTALL_PREFIX="/usr")

set(CMAKE_C_FLAGS_DEBUG "-O0 -ggdb -Wall" CACHE STRING "" FORCE)

add_library(autostatus SHARED autostatus.c)
install(TARGETS autostatus LIBRARY DESTINATION lib/pidgin NAMELINK_SKIP)

find_package( PkgConfig REQUIRED )

pkg_check_modules( LIBPURPLE REQUIRED purple>=2.4.0 )
pkg_check_modules( GLIB REQUIRED glib-2.0 )
pkg_check_modules( DBUS_GLIB REQUIRED dbus-glib-1 )
include_directories(${LIBPURPLE_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS} ${DBUS_GLIB_INCLUDE_DIRS})
target_link_libraries(autostatus ${LIBPURPLE_LIBRARIES} ${GLIB_LIBRARIES} ${DBUS_GLIB_LIBRARIES})

include(FeatureSummary)
print_enabled_features()
