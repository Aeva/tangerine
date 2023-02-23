# SPDX-License-Identifier: (Apache-2.0 OR MIT)
# SPDX-FileCopyrightText: © 2023 Philip McGrath <philip@philipmcgrath.com>

#[=======================================================================[.rst:

FindLZ4
-------

Finds the LZ4 library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``LZ4::LZ4``
  The LZ4 library

Output Variables
^^^^^^^^^^^^^^^^

The following output variables are set:

``Racket_FOUND``
  True if the system has LZ4.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``LZ4_LIBRARY``
  Path to ``LZ4::LZ4``.
``LZ4_INCLUDE_DIR``
  Include directory for ``<lz4.h>``, if found.

#]=======================================================================]

# Model: https://cmake.org/cmake/help/latest/manual/cmake-developer.7.html#find-modules

include(FindPackageHandleStandardArgs)

find_package(PkgConfig)
pkg_check_modules(PC_LZ4 QUIET liblz4)
set(LZ4_VERSION ${PC_LZ4_VERSION})
find_library(LZ4_LIBRARY
  NAMES lz4
  PATHS ${PC_LZ4_LIBRARY_DIRS}
  PATH_SUFFIXES lz4
  DOC "The LZ4 library."
)
find_path(LZ4_INCLUDE_DIR
  NAMES lz4.h
  PATHS ${PC_LZ4_INCLUDE_DIRS}
  PATH_SUFFIXES lz4
  DOC "The include directory for <lz4.h>."
)
find_package_handle_standard_args(LZ4
  REQUIRED_VARS
    LZ4_LIBRARY
  VERSION_VAR LZ4_VERSION
)
if(LZ4_FOUND AND NOT TARGET LZ4::LZ4)
    add_library(LZ4::LZ4 UNKNOWN IMPORTED)
    set_target_properties(LZ4::LZ4 PROPERTIES
        IMPORTED_LOCATION "${LZ4_LIBRARY}"
    )
    if(LZ4_INCLUDE_DIR)
        target_include_directories(LZ4::LZ4 INTERFACE "${LZ4_INCLUDE_DIR}")
    endif()
endif()
