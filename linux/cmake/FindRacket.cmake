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

function(racket_extract_config_pair entryId)
    # FRAGILE: breaks if there is a ")" in the string
    # Also, the result might not `read` as a Racket pair, e.g. for `(k . ())`
    set(_options "")
    set(_oneValKeys FROM OUTPUT_VARIABLE)
    set(_multiValKeys "")
    cmake_parse_arguments(PARSE_ARGV 1 "parse" "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    # parse_KEYWORDS_MISSING_VALUES
    # message(WARNING "unparsed: ${parse_UNPARSED_ARGUMENTS}")
    # message(WARNING "missing: ${parse_KEYWORDS_MISSING_VALUES}")
    string(REGEX MATCH "[(][ \t\r\n]*${entryId}[ \t\r\n]+\.[ \t\r\n]+[^)]+[)]"
        _found
        ${parse_FROM}
    )
    set(${parse_OUTPUT_VARIABLE} ${_found} PARENT_SCOPE)
endfunction()

function(racket_extract_strings)
    # FRAGILE: breaks if there is a "\"" in the string
    set(_options "")
    set(_oneValKeys FROM OUTPUT_VARIABLE)
    set(_multiValKeys "")
    cmake_parse_arguments(PARSE_ARGV 0 "parse" "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    # parse_KEYWORDS_MISSING_VALUES
    # message(WARNING "unparsed: ${parse_UNPARSED_ARGUMENTS}")
    # message(WARNING "missing: ${parse_KEYWORDS_MISSING_VALUES}")
    string(REGEX MATCHALL "\"[^\"]*[\"]"
        _allRaw
        ${parse_FROM}
    )
    set(_allTrimmed "")
    foreach(_raw IN LISTS _allRaw)
        # remove the opening and closing "\""
        string(LENGTH ${_raw} _lenRaw)
        math(EXPR _lenTrimmed "${_lenRaw} - 2")
        string(SUBSTRING ${_raw} 1 ${_lenTrimmed} _trimmed)
        list(APPEND _allTrimmed ${_trimmed})
    endforeach()
    set(${parse_OUTPUT_VARIABLE} ${_allTrimmed} PARENT_SCOPE)
endfunction()

function(racket_parse_config_entry entryId)
    # Not correct in general (see comments above), but good enough for hints
    set(_options "")
    set(_oneValKeys FROM OUTPUT_VARIABLE)
    set(_multiValKeys "")
    cmake_parse_arguments(PARSE_ARGV 1 "parse" "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    # parse_KEYWORDS_MISSING_VALUES
    # message(WARNING "unparsed: ${parse_UNPARSED_ARGUMENTS}")
    # message(WARNING "missing: ${parse_KEYWORDS_MISSING_VALUES}")
    racket_extract_config_pair(${entryId} FROM ${parse_FROM} OUTPUT_VARIABLE _pair)
    racket_extract_strings(FROM ${_pair} OUTPUT_VARIABLE _string)
    #message(NOTICE "========\nFound:\n-----\n${entryId}\n${_string}\n========")
    set(${parse_OUTPUT_VARIABLE} ${_string} PARENT_SCOPE)
endfunction()

function(racket_parse_config_search_path dirId searchDirsID)
    # Not correct in general (see comments above), but good enough for hints
    set(_options "")
    set(_oneValKeys FROM OUTPUT_VARIABLE)
    set(_multiValKeys "")
    cmake_parse_arguments(PARSE_ARGV 2 "parse" "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    # parse_KEYWORDS_MISSING_VALUES
    # message(WARNING "unparsed: ${parse_UNPARSED_ARGUMENTS}")
    # message(WARNING "missing: ${parse_KEYWORDS_MISSING_VALUES}")
    racket_parse_config_entry(${dirId}
        FROM ${parse_FROM}
        OUTPUT_VARIABLE _ret
    )
    racket_parse_config_entry(${searchDirsID}
        FROM ${parse_FROM}
        OUTPUT_VARIABLE _more
    )
    list(APPEND _ret "${_more}")
    set(${parse_OUTPUT_VARIABLE} ${_ret} PARENT_SCOPE)
endfunction()

file(READ ${Racket_CONFIG_FILE} _Racket_CONFIG_FILE_content)
racket_parse_config_entry("installation-name"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _installationName
)
racket_parse_config_search_path("lib-dir" "lib-search-dirs"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _configLibSearchDirs
)
racket_parse_config_search_path("include-dir" "include-search-dirs"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _configIncludeSearchDirs
)
#message(WARNING "Got:\n${_configLibSearchDirs}")
#message(NOTICE "Got:\n${_configIncludeSearchDirs}")


find_library(Racket_LIBRARY
  NAMES racketcs libracketcs NAMES_PER_DIR
  HINTS ${_configLibSearchDirs}
  PATH_SUFFIXES "racket"
)
message(WARNING ${Racket_LIBRARY})
find_path(Racket_INCLUDE_DIR
  NAMES racketcs.h
  PATHS ${_configIncludeSearchDirs}
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
