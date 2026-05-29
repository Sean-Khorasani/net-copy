# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/src/net_copy/build_vs/_deps/liboqs-src")
  file(MAKE_DIRECTORY "D:/src/net_copy/build_vs/_deps/liboqs-src")
endif()
file(MAKE_DIRECTORY
  "D:/src/net_copy/build_vs/_deps/liboqs-build"
  "D:/src/net_copy/build_vs/_deps/liboqs-subbuild/liboqs-populate-prefix"
  "D:/src/net_copy/build_vs/_deps/liboqs-subbuild/liboqs-populate-prefix/tmp"
  "D:/src/net_copy/build_vs/_deps/liboqs-subbuild/liboqs-populate-prefix/src/liboqs-populate-stamp"
  "D:/src/net_copy/build_vs/_deps/liboqs-subbuild/liboqs-populate-prefix/src"
  "D:/src/net_copy/build_vs/_deps/liboqs-subbuild/liboqs-populate-prefix/src/liboqs-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/src/net_copy/build_vs/_deps/liboqs-subbuild/liboqs-populate-prefix/src/liboqs-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/src/net_copy/build_vs/_deps/liboqs-subbuild/liboqs-populate-prefix/src/liboqs-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
