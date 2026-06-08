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
