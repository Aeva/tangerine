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
            set(Racket_CONFIG_FILE ${_file})
            break()
        endif()
    endforeach()
#endif()

file(READ ${_Racket_CONFIG_FILE} _Racket_CONFIG_FILE_content NEWLINE_CONSUME)

string(REGEX MATCH "[(][ \t\r\n]*lib-search-dirs[ \t\r\n]+\.[ \t\r\n]+[^)]+[)]"
    _matched
    ${_Racket_CONFIG_FILE_content})
message(FATAL_ERROR "Matched:\n--------\n${_matched}\n--------}")


find_package_handle_standard_args(Racket DEFAULT_MSG Racket_CONFIG_DIR)



