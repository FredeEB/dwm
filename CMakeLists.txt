cmake_minimum_required(VERSION 3.18)
project(dwm)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# find dependencies
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(X11 COMPONENTS Xft Xinerama REQUIRED)

# the dwm executable
add_executable(dwm
  dwm.cpp
  drw.cpp
  util.cpp)

# link to libraries
target_link_libraries(dwm
  Freetype::Freetype
  Fontconfig::Fontconfig
  X11::Xft
  X11::Xinerama
  )

# get dwm version from git tag
execute_process(
    COMMAND git log -1 --format=%h
    OUTPUT_VARIABLE VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ECHO_OUTPUT_VARIABLE)

target_compile_options(dwm PUBLIC -Wall -Wextra -Wno-deprecated-declarations)
target_compile_features(dwm PUBLIC cxx_std_20 c_std_17)
target_compile_definitions(dwm PUBLIC "-DVERSION=\"${VERSION}\"")

install(TARGETS dwm)
install(FILES dwm.desktop DESTINATION /usr/share/xsessions)
install(PROGRAMS autostart.sh DESTINATION /etc/dwm)
