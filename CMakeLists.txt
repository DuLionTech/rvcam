cmake_minimum_required(VERSION 3.28)
project(basic C)

set(CMAKE_C_STANDARD 17)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

include_directories(include)
include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})

add_executable(${PROJECT_NAME}_stream main_stream.c)

target_link_libraries(${PROJECT_NAME}_stream PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
