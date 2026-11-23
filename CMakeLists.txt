# Do not let CMake scan for C++ modules or build the "import std" helper target: with the NDK toolchain that helper tries to
# compile the host libstdc++ std.cc and fails ("bits/stdc++.h file not found"). The mod uses plain #include only.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_MODULE_STD OFF)
add_yeeps_mod(leos-menu src/menu.cpp)
if(TARGET leos-menu)
  set_target_properties(leos-menu PROPERTIES CXX_SCAN_FOR_MODULES OFF CXX_MODULE_STD OFF)
endif()
