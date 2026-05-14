cmake_minimum_required(VERSION 4.2)
project(basic C)

set(CMAKE_C_STANDARD 17)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)

include_directories(include)

add_executable(${PROJECT_NAME}_2 main_2.c)
add_executable(${PROJECT_NAME}_3 main_3.c)
add_executable(${PROJECT_NAME}_4 main_4.c)

target_link_libraries(${PROJECT_NAME}_2 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
target_link_libraries(${PROJECT_NAME}_3 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
target_link_libraries(${PROJECT_NAME}_4 PRIVATE PkgConfig::GSTREAMER PkgConfig::GST_APP)
