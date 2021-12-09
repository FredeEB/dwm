project(dwm)
cmake_minimum_required(VERSION 3.18)

# set standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# find dependencies
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(X11 COMPONENTS Xft Xinerama REQUIRED)

# the dwm executable
add_executable(dwm
  dwm.c
  drw.c
  util.c)

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
target_compile_definitions(dwm 
    PUBLIC "-DXINERAMA"
    PUBLIC "-DVERSION=\"${VERSION}\""
    )

install(TARGETS dwm)
install(FILES dwm.desktop DESTINATION /usr/share/xsessions)
install(PROGRAMS autostart.sh DESTINATION /etc/dwm)
