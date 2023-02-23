# SPDX-License-Identifier: (Apache-2.0 OR MIT)
# SPDX-FileCopyrightText: © 2023 Philip McGrath <philip@philipmcgrath.com>

#[=======================================================================[.rst:

FindRacket
----------

Finds Racket. Currently focused on Racket CS for embedding.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``Racket::LibRacketCS``
  The Racket CS library

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``Racket_PETITE_BOOT``
  Path to ``petite.boot``.
``Racket_SCHEME_BOOT``
  Path to ``scheme.boot``.
``Racket_RACKET_BOOT``
  Path to ``racket.boot``.
``Racket_CONFIG_DIR``
  Path to a main configuration directory like the ``-G``/``--config`` option to ``racket``.
``Racket_COLLECTS_DIR``
  Path to a main collections directory like the ``-X``/``--collects`` option to ``racket``.

Less Useful Variables
^^^^^^^^^^^^^^^^^^^^^

These cache variables may also be set, but they are less likely to be useful to your code:

``Racket_racketcs_LIBRARY``
  Path to the library ``Racket::LibRacketCS``.
``Racket_racketcs_INCLUDE_DIR``
  Path to the directory for ``racketcs.h``.
``Racket_chezscheme_INCLUDE_DIR``
  Likewise, but for ``chezscheme.h`` (likely the same directory).

The following non-cache output variables will be set (but prefer ``Racket::LibRacketCS``):

``Racket_FOUND``
  True if the system has Racket.
``Racket_INCLUDE_DIRS``
  Include directories needed to embed Racket.
``Foo_LIBRARIES``
  Libraries needed to link to Racket (i.e. ``Racket::LibRacketCS``).

#]=======================================================================]

# Model: https://cmake.org/cmake/help/latest/manual/cmake-developer.7.html#find-modules

include(FindPackageHandleStandardArgs)
include(RacketParseConfigFile)

cmake_path(CONVERT "$ENV{Racket_ROOT}" TO_CMAKE_PATH_LIST _env_Racket_ROOT)
cmake_path(CONVERT "$ENV{CMAKE_PREFIX_PATH}" TO_CMAKE_PATH_LIST _env_CMAKE_PREFIX_PATH)
set(_toSearch "")
foreach(dir IN LISTS Racket_ROOT _env_Racket_ROOT CMAKE_PREFIX_PATH _env_CMAKE_PREFIX_PATH)
    list(APPEND _toSearch ${dir})
endforeach()
cmake_path(CONVERT "$ENV{LIB}" TO_CMAKE_PATH_LIST _env_LIB)
cmake_path(CONVERT "$ENV{INCLUDE}" TO_CMAKE_PATH_LIST _env_INCLUDE)
cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST _env_PATH)
foreach(dir IN LISTS _env_LIB _env_INCLUDE _env_PATH)
    cmake_path(GET dir PARENT_PATH _updir)
    list(APPEND _toSearch ${_updir})
endforeach()
foreach(dir IN LISTS CMAKE_INSTALL_PREFIX CMAKE_STAGING_PREFIX CMAKE_SYSTEM_PREFIX_PATH)
    list(APPEND _toSearch ${dir})
endforeach()
list(REMOVE_DUPLICATES _toSearch)

set(Racket_CONFIG_FILE NO)

#if(NOT $CACHE{Racket_CONFIG_DIR})
# FIXME maybe https://cmake.org/cmake/help/latest/command/cmake_language.html#defer
    set(_toSearch_etc_racket "")
    set(_toSearch_etc "")
    foreach(dir IN LISTS _toSearch)
        cmake_path(APPEND dir "etc" OUTPUT_VARIABLE _etc)
        cmake_path(APPEND _etc "racket" OUTPUT_VARIABLE _rkt)
        list(APPEND _toSearch_etc_racket ${_rkt})
        list(APPEND _toSearch_etc ${_etc})
    endforeach()
    foreach(dir IN LISTS _toSearch_etc_racket _toSearch_etc)
        cmake_path(APPEND dir "config.rktd" OUTPUT_VARIABLE _file)
        if(EXISTS ${_file})
            set(Racket_CONFIG_DIR ${dir} CACHE PATH "Like the --config argument at the command line.")
            set(Racket_CONFIG_FILE ${_file}) # FIXME set this outside to work even if we got Racket_CONFIG_DIR from the user (we may still need to find others
            break()
        endif()
    endforeach()
#endif()


racket_parse_config_file(${Racket_CONFIG_FILE}
    NAME _installationName
    LIB_DIRS _configLibDirs
    INCLUDE_DIRS _configIncludeDirs
)


find_library(Racket_racketcs_LIBRARY
  NAMES racketcs libracketcs NAMES_PER_DIR
  HINTS ${_configLibDirs}
  PATH_SUFFIXES "racket"
  DOC "Racket CS library for embedding."
)


find_path(Racket_racketcs_INCLUDE_DIR
  NAMES racketcs.h
  HINTS ${_configIncludeDirs}
  PATH_SUFFIXES "Directory to include for <racketcs.h>."
)
# Prefer the sibling of "racketcs.h" if it exists, search otherwise.
find_path(Racket_chezscheme_INCLUDE_DIR
  NAMES chezscheme.h
  HINTS ${Racket_racketcs_INCLUDE_DIR}
  NO_DEFAULT_PATH
  PATH_SUFFIXES "Directory to include for <chezscheme.h>."
)
find_path(Racket_chezscheme_INCLUDE_DIR
  NAMES chezscheme.h
  HINTS ${_configIncludeDirs}
  PATH_SUFFIXES "Directory to include for <chezscheme.h>."
)


# We would like to use the default paths from `find_library`,
# but it insists on appending ".so" or ".a".
# No PATH_SUFFIXES needed as _configLibDirs handles it.
find_file(Racket_PETITE_BOOT
  "petite.boot"
  HINTS ${_configLibDirs}
  NO_DEFAULT_PATH
  DOC "Bootfile for Petite Chez Scheme."
)
find_file(Racket_SCHEME_BOOT
  "scheme.boot"
  HINTS ${_configLibDirs}
  NO_DEFAULT_PATH
  DOC "Bootfile  for Chez Scheme."
)
find_file(Racket_RACKET_BOOT
  "racket.boot"
  HINTS ${_configLibDirs}
  NO_DEFAULT_PATH
  DOC "Bootfile for Racket."
)

find_path(Racket_COLLECTS_DIR
    "racket/base.rkt"
    HINTS ${_configLibDirs}
    PATH_SUFFIXES "../collects" "../../collects" "../../share/racket/collects" "../share/racket/collects"
    DOC "Like the --collects argument at the command line."
)


find_package_handle_standard_args(Racket
    REQUIRED_VARS
        Racket_CONFIG_DIR Racket_COLLECTS_DIR
        Racket_racketcs_LIBRARY
        Racket_racketcs_INCLUDE_DIR Racket_chezscheme_INCLUDE_DIR
        Racket_PETITE_BOOT Racket_SCHEME_BOOT Racket_RACKET_BOOT
)
mark_as_advanced(
    Racket_CONFIG_DIR
)
if(Racket_FOUND AND NOT TARGET Racket::LibRacketCS)
    # per cmake docs, Racket_INCLUDE_DIRS "should not be a cache entry"
    set(Racket_INCLUDE_DIRS ${Racket_racketcs_INCLUDE_DIR} ${Racket_chezscheme_INCLUDE_DIR})
    list(REMOVE_DUPLICATES Racket_INCLUDE_DIRS)
    find_package(ZLIB)
    find_package(LZ4)
    add_library(Racket::LibRacketCS UNKNOWN IMPORTED)
    set_target_properties(Racket::LibRacketCS PROPERTIES
        IMPORTED_LOCATION ${Racket_racketcs_LIBRARY}
    )
    target_include_directories(Racket::LibRacketCS
        INTERFACE "${Racket_INCLUDE_DIRS}"
    )
    target_link_libraries(Racket::LibRacketCS
        INTERFACE ZLIB::ZLIB LZ4::LZ4
    )
    set(Racket_LIBRARIES Racket::LibRacketCS)
endif()
