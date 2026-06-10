include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Shared compile options for all test targets
# ---------------------------------------------------------------------------
add_compile_options(-Wall -Wextra -O0 -g)

# ---------------------------------------------------------------------------
# test_config — exercises the INI config parser (no SDL2, no libcamera)
# ---------------------------------------------------------------------------
add_executable(test_config
    test_config.cpp
    ${CMAKE_SOURCE_DIR}/src/config/config.cpp
)
target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_config PRIVATE GTest::gtest_main)
add_test(NAME config COMMAND test_config)

# ---------------------------------------------------------------------------
# test_keybinding — exercises the key-name parser (SDL2 headers, no SDL_Init)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(test_keybinding
    test_keybinding.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/keybinding.cpp
)
target_include_directories(test_keybinding PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_keybinding PRIVATE GTest::gtest_main ${SDL2_LIBRARIES})
add_test(NAME keybinding COMMAND test_keybinding)

# ---------------------------------------------------------------------------
# test_exposure — exercises the shutter/ISO ladder helpers (header-only, no deps)
# ---------------------------------------------------------------------------
add_executable(test_exposure
    test_exposure.cpp
)
target_include_directories(test_exposure PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_exposure PRIVATE GTest::gtest_main)
add_test(NAME exposure COMMAND test_exposure)

# ---------------------------------------------------------------------------
# test_socket_server — exercises the Unix domain socket server (POSIX only)
# ---------------------------------------------------------------------------
add_executable(test_socket_server
    test_socket_server.cpp
    ${CMAKE_SOURCE_DIR}/src/util/socket_server.cpp
)
target_include_directories(test_socket_server PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_socket_server PRIVATE GTest::gtest_main)
add_test(NAME socket_server COMMAND test_socket_server)

# ---------------------------------------------------------------------------
# test_mjpeg_server — exercises the HTTP-MJPEG server (libjpeg-turbo + OpenSSL)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBJPEG REQUIRED libturbojpeg)
find_package(OpenSSL REQUIRED)

add_executable(test_mjpeg_server
    test_mjpeg_server.cpp
    ${CMAKE_SOURCE_DIR}/src/util/mjpeg_server.cpp
)
target_include_directories(test_mjpeg_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LIBJPEG_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)
target_link_libraries(test_mjpeg_server PRIVATE
    GTest::gtest_main
    ${LIBJPEG_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
)
add_test(NAME mjpeg_server COMMAND test_mjpeg_server)

# ---------------------------------------------------------------------------
# test_timelapse — exercises interval schedule functions (header-only, no deps)
# ---------------------------------------------------------------------------
add_executable(test_timelapse
    test_timelapse.cpp
)
target_include_directories(test_timelapse PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_timelapse PRIVATE GTest::gtest_main)
add_test(NAME timelapse COMMAND test_timelapse)

# ---------------------------------------------------------------------------
# test_resolution — aspect_str and select_from_modes (SDL2 for resolution.cpp)
# ---------------------------------------------------------------------------
add_executable(test_resolution
    test_resolution.cpp
    ${CMAKE_SOURCE_DIR}/src/util/resolution.cpp
)
target_include_directories(test_resolution PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_resolution PRIVATE GTest::gtest_main ${SDL2_LIBRARIES})
add_test(NAME resolution COMMAND test_resolution)

# ---------------------------------------------------------------------------
# test_tl_dialog — timelapse dialog callback state machine (no deps)
# ---------------------------------------------------------------------------
add_executable(test_tl_dialog
    test_tl_dialog.cpp
)
target_include_directories(test_tl_dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tl_dialog PRIVATE GTest::gtest_main)
add_test(NAME tl_dialog COMMAND test_tl_dialog)
