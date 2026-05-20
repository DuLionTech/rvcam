cmake_minimum_required(VERSION 3.28)
project(basic C)

set(CMAKE_C_STANDARD 17)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(GST_AUDIO REQUIRED gstreamer-audio-1.0)

include_directories(include)
include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})

include_directories(${GST_AUDIO_INCLUDE_DIRS})

add_executable(${PROJECT_NAME}_2 main_2.c)
add_executable(${PROJECT_NAME}_3 main_3.c)
add_executable(${PROJECT_NAME}_4 main_4.c)
add_executable(${PROJECT_NAME}_5 main_5.c)
add_executable(${PROJECT_NAME}_6 main_6.c)
add_executable(${PROJECT_NAME}_7 main_7.c)
add_executable(${PROJECT_NAME}_8 main_8.c)

target_link_libraries(${PROJECT_NAME}_2 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
target_link_libraries(${PROJECT_NAME}_3 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
target_link_libraries(${PROJECT_NAME}_4 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
target_link_libraries(${PROJECT_NAME}_5 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP ${GTK3_LIBRARIES})
target_link_libraries(${PROJECT_NAME}_6 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
target_link_libraries(${PROJECT_NAME}_7 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
target_link_libraries(${PROJECT_NAME}_8 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP ${GST_AUDIO_LIBRARIES})
