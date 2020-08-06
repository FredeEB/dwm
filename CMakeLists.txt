project(dwm)
cmake_minimum_required(VERSION 3.10)

# set standard
set(CMAKE_C_STANDARD 99)

# find dependencies
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(X11 COMPONENTS Xft Xinerama REQUIRED)

# the dwm executable
add_executable(dwm
  dwm.c
  drw.c
  util.c)

# get dwm version from git tag
execute_process(COMMAND "git describe --abbrev=0 --tags | sed -E 's/\n//'" OUTPUT_VARIABLE GIT_TAG)
configure_file(version.h.in ${PROJECT_SOURCE_DIR}/version.h)

# link to libraries
target_link_libraries(dwm
  Freetype::Freetype
  Fontconfig::Fontconfig
  X11::Xft
  X11::Xinerama)

target_compile_options(dwm PUBLIC -Wall -Wextra -Wno-deprecated-declarations)
target_compile_definitions(dwm PUBLIC XINERAMA)

install(TARGETS dwm)
install(FILES dwm.desktop DESTINATION /usr/share/xsessions)
install(PROGRAMS autostart.sh DESTINATION /etc/dwm)
