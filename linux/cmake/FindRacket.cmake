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

#]=======================================================================]

# Model: https://cmake.org/cmake/help/latest/manual/cmake-developer.7.html#find-modules

include(FindPackageHandleStandardArgs)
include(RacketParseConfigFile.cmake)

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
            set(Racket_CONFIG_DIR ${dir} CACHE PATH "Like the --config argument at the command line")
            set(Racket_CONFIG_FILE ${_file}) # FIXME set this outside to work even if we got Racket_CONFIG_DIR from the user (we may still need to find others
            break()
        endif()
    endforeach()
#endif()

racket_parse_config_file("/gnu/store/al90nb7vrjhgbzr71x037c7ny73b03sb-tangerine-racket-layer-8.7/etc/racket/config.rktd"
    NAME _installationName
    LIB_DIRS _configLibDirs
    INCLUDE_DIRS _configIncludeDirs
)
#message(WARNING "Got:\n${_configLibSearchDirs}")
#message(NOTICE "Got:\n${_configIncludeSearchDirs}")


find_library(Racket_LIBRARY
  NAMES racketcs libracketcs NAMES_PER_DIR
  HINTS ${_configLibDirs}
  PATH_SUFFIXES "racket"
)
message(WARNING ${Racket_LIBRARY})
find_path(Racket_INCLUDE_DIR
  NAMES racketcs.h
  HINTS ${_configIncludeDirs}
  PATH_SUFFIXES "racket"
)
message(WARNING ${Racket_INCLUDE_DIR})
######## These don't work:
# find_library(Racket_petiteBoot
#   "petite.boot"
#   HINTS ${_configLibSearchDirs}
# )
# find_library(Racket_schemeBoot
#   "scheme.boot"
#   HINTS ${_configLibSearchDirs}
# )
# find_library(Racket_racketBoot
#   "racket.boot"
#   HINTS ${_configLibSearchDirs}
# )



find_package_handle_standard_args(Racket DEFAULT_MSG
    Racket_CONFIG_DIR
    Racket_LIBRARY
    # Racket_petiteBoot
    # Racket_schemeBoot
    # Racket_racketBoot
    Racket_INCLUDE_DIR
)
message(FATAL_ERROR "stop here")

mark_as_advanced(
    Racket_CONFIG_DIR
)
