# CMake generated Testfile for 
# Source directory: C:/ExoSim
# Build directory: C:/ExoSim/build-release
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[physics_and_data_validation]=] "C:/ExoSim/build-release/Debug/ExoSimTests.exe")
  set_tests_properties([=[physics_and_data_validation]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/ExoSim/CMakeLists.txt;73;add_test;C:/ExoSim/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[physics_and_data_validation]=] "C:/ExoSim/build-release/Release/ExoSimTests.exe")
  set_tests_properties([=[physics_and_data_validation]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/ExoSim/CMakeLists.txt;73;add_test;C:/ExoSim/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[physics_and_data_validation]=] "C:/ExoSim/build-release/MinSizeRel/ExoSimTests.exe")
  set_tests_properties([=[physics_and_data_validation]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/ExoSim/CMakeLists.txt;73;add_test;C:/ExoSim/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[physics_and_data_validation]=] "C:/ExoSim/build-release/RelWithDebInfo/ExoSimTests.exe")
  set_tests_properties([=[physics_and_data_validation]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/ExoSim/CMakeLists.txt;73;add_test;C:/ExoSim/CMakeLists.txt;0;")
else()
  add_test([=[physics_and_data_validation]=] NOT_AVAILABLE)
endif()
