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
    cmake_parse_arguments(PARSE_ARGV 2 parse "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    # parse_KEYWORDS_MISSING_VALUES
    string(REGEX MATCH "[(][ \t\r\n]*${entryId}[ \t\r\n]+\.[ \t\r\n]+[^)]+[)]"
        _found
        ${parse_FROM}
    )
    set(${parse_OUTPUT_VARIABLE} ${_found} PARENT_SCOPE)
endfunction()

function(racket_extract_last_string)
    # FRAGILE: breaks if there is a "\"" in the string
    set(_options "")
    set(_oneValKeys FROM OUTPUT_VARIABLE)
    set(_multiValKeys "")
    cmake_parse_arguments(PARSE_ARGV 2 "parse" "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    # parse_KEYWORDS_MISSING_VALUES
    string(REGEX MATCHALL "\"[^\"]*[\"]"
        _allRaw
        ${parse_FROM}
    )
    list(POP_BACK _allRaw _lastRaw) # NOTE: mutates _allRaw
    # remove the opening and closing "\""
    string(LENGTH ${_lastRaw} _lenRaw)
    math(EXPR _lenTrimmed "${_lenRaw} - 2")
    string(SUBSTRING ${_lastRaw} 1 ${_lenTrimmed} _lastTrimmed)
    set(${parse_OUTPUT_VARIABLE} ${_lastTrimmed} PARENT_SCOPE)
endfunction()

function(racket_parse_config_entry entryId)
    # Not correct in general (see comments above), but good enough for hints
    set(_options "")
    set(_oneValKeys FROM OUTPUT_VARIABLE)
    set(_multiValKeys "")
    cmake_parse_arguments(PARSE_ARGV 2 "parse" "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    # parse_KEYWORDS_MISSING_VALUES
    racket_extract_config_pair(${entryId} FROM ${parse_FROM} OUTPUT_VARIABLE _pair)
    racket_extract_last_string(FROM ${_pair} OUTPUT_VARIABLE _string)
    message(WARNING "========\nFound:\n-----\n${entryId}\n${_string}\n========")
    set(${parse_OUTPUT_VARIABLE} ${_string} PARENT_SCOPE)
endfunction()

# message(WARNING "File   :\n--------\n${Racket_CONFIG_FILE}\n--------")
# file(READ ${Racket_CONFIG_FILE} _Racket_CONFIG_FILE_content)
# message(WARNING "Content:\n--------\n${_Racket_CONFIG_FILE_content}\n--------")
# string(REGEX MATCH "[(][ \t\r\n]*lib-search-dirs[ \t\r\n]+\.[ \t\r\n]+[^)]+[)]"
#     _matched
#     ${_Racket_CONFIG_FILE_content})
# message(WARNING "ContenT:\n--------\n${_Racket_CONFIG_FILE_content}\n--------")
# message(WARNING "Matched:\n--------\n${_matched}\n--------}")

racket_parse_config_entry("installation-name"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _installationName
)
racket_parse_config_entry("lib-dir"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _configLibDir
)
racket_parse_config_entry("lib-search-dirs"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _configLastLibSearchDir
)
racket_parse_config_entry("include-dir"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _configIncludeDir
)
racket_parse_config_entry("include-search-dirs"
    FROM ${_Racket_CONFIG_FILE_content}
    OUTPUT_VARIABLE _configLastIncludeSearchDir
)

find_package_handle_standard_args(Racket DEFAULT_MSG Racket_CONFIG_DIR)
message(FATAL_ERROR "stop here")

mark_as_advanced(
    Racket_CONFIG_DIR
)
