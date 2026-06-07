cmake_minimum_required(VERSION 3.18)
project(microscopi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Unit tests (host build — skips the app, does not require libcamera)
# Run: cmake -S . -B build-tests -DBUILD_TESTS=ON && cmake --build build-tests
#      ctest --test-dir build-tests --output-on-failure
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build unit tests (host-only, skips the app)" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    return()
endif()

# ---------------------------------------------------------------------------
# Dependencies (pkg-config)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBCAMERA   REQUIRED libcamera)
pkg_check_modules(SDL2        REQUIRED sdl2)
pkg_check_modules(SDL2_TTF    REQUIRED SDL2_ttf)
pkg_check_modules(SDL2_IMAGE  REQUIRED SDL2_image)
pkg_check_modules(AVFORMAT    REQUIRED libavformat)
pkg_check_modules(AVCODEC     REQUIRED libavcodec)
pkg_check_modules(AVUTIL      REQUIRED libavutil)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
add_executable(microscopi
    src/main.cpp
    src/camera/camera.cpp
    src/camera/encoder.cpp
    src/ui/renderer.cpp
    src/ui/osd.cpp
    src/ui/input.cpp
    src/ui/keybinding.cpp
    src/config/config.cpp
    src/util/resolution.cpp
    src/util/exif_writer.cpp
    src/util/socket_server.cpp
)

target_include_directories(microscopi PRIVATE
    src
    ${LIBCAMERA_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

target_link_libraries(microscopi PRIVATE
    ${LIBCAMERA_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

target_compile_options(microscopi PRIVATE
    ${LIBCAMERA_CFLAGS_OTHER}
    ${SDL2_CFLAGS_OTHER}
    -Wall -Wextra
    $<IF:$<CONFIG:Debug>,-O0 -g3 -fno-omit-frame-pointer,-O2>
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS microscopi
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY assets/icons/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/microscopi/icons
    FILES_MATCHING PATTERN "*.svg")

install(FILES assets/fonts/RobotoCondensed-Regular.ttf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/microscopi/fonts)

# Default config goes into sysconfdir (e.g. /usr/local/etc or /etc
# depending on prefix). stage3/01-run.sh copies it to /etc/microscopi.conf.
install(FILES config/microscopi.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
    RENAME microscopi.conf)
