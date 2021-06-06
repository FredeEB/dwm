project(dwm)
cmake_minimum_required(VERSION 3.10)

# set standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# find dependencies
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(X11 COMPONENTS Xft Xinerama REQUIRED)

# the dwm executable
add_executable(
  dwm
  dwm.cpp
  drw.cpp
  util.cpp
)

add_executable(
  dwm-msg
  dwm-msg.cpp
)

# get dwm version from git tag
execute_process(COMMAND "git describe --abbrev=0 --tags | sed -E 's/\n//'" OUTPUT_VARIABLE GIT_TAG)
configure_file(version.hpp.in ${PROJECT_SOURCE_DIR}/version.hpp)

# link to libraries
target_link_libraries(
  dwm
  Freetype::Freetype
  Fontconfig::Fontconfig
  X11::Xft
  X11::Xinerama
)

# TODO Remove yajl stuff
target_include_directories(dwm PUBLIC include /usr/include/yajl)
target_link_options(dwm PUBLIC -lyajl)

target_include_directories(dwm-msg PUBLIC include /usr/include/yajl)
target_link_options(dwm-msg PUBLIC -lyajl)

target_compile_options(dwm PUBLIC -Wall -Wextra -Wno-deprecated-declarations)
target_compile_definitions(dwm PUBLIC XINERAMA)

install(TARGETS dwm dwm-msg)
install(FILES dwm.desktop DESTINATION /usr/share/xsessions)
install(PROGRAMS autostart.sh DESTINATION /etc/dwm)
