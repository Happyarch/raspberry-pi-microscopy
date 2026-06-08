# CMake generated Testfile for 
# Source directory: /mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests
# Build directory: /mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/build-tests/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(config "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/build-tests/tests/test_config")
set_tests_properties(config PROPERTIES  _BACKTRACE_TRIPLES "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;25;add_test;/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;0;")
add_test(keybinding "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/build-tests/tests/test_keybinding")
set_tests_properties(keybinding PROPERTIES  _BACKTRACE_TRIPLES "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;42;add_test;/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;0;")
add_test(exposure "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/build-tests/tests/test_exposure")
set_tests_properties(exposure PROPERTIES  _BACKTRACE_TRIPLES "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;52;add_test;/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;0;")
add_test(socket_server "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/build-tests/tests/test_socket_server")
set_tests_properties(socket_server PROPERTIES  _BACKTRACE_TRIPLES "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;63;add_test;/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;0;")
add_test(mjpeg_server "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/build-tests/tests/test_mjpeg_server")
set_tests_properties(mjpeg_server PROPERTIES  _BACKTRACE_TRIPLES "/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;87;add_test;/mnt/sdb1/Code/Active Code/raspberry-pi-microscopy/tests/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
