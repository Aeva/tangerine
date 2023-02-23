# SPDX-License-Identifier: (Apache-2.0 OR MIT)
# SPDX-FileCopyrightText: © 2023 Philip McGrath <philip@philipmcgrath.com>

#[=======================================================================[.rst:

RacketParseConfigFile
---------------------

#]=======================================================================]

function(racket_extract_config_pair entryId)
    # FRAGILE: breaks if there is a ")" in the string
    # Also, note that the result might not `read` as a Racket pair, e.g. for `(k . ())`
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

function(racket_parse_config_file Racket_CONFIG_FILE)
    set(_options "")
    set(_oneValKeys NAME LIB_DIRS INCLUDE_DIRS)
    set(_multiValKeys "")
    cmake_parse_arguments(PARSE_ARGV 1 "parse" "${_options}" "${_oneValKeys}" "${_multiValKeys}")
    # parse_UNPARSED_ARGUMENTS
    file(READ ${Racket_CONFIG_FILE} _Racket_CONFIG_FILE_content)
    if(DEFINED parse_NAME)
        racket_parse_config_entry("installation-name"
            FROM ${_Racket_CONFIG_FILE_content}
            OUTPUT_VARIABLE _installationName
        )
        set(${parse_NAME} ${_installationName} PARENT_SCOPE)
    endif()
    if(DEFINED parse_LIB_DIRS)
        racket_parse_config_search_path("lib-dir" "lib-search-dirs"
            FROM ${_Racket_CONFIG_FILE_content}
            OUTPUT_VARIABLE _configLibSearchDirs
        )
        set(${parse_LIB_DIRS} ${_configLibSearchDirs} PARENT_SCOPE)
    endif()
    if(DEFINED parse_INCLUDE_DIRS)
        racket_parse_config_search_path("include-dir" "include-search-dirs"
            FROM ${_Racket_CONFIG_FILE_content}
            OUTPUT_VARIABLE _configIncludeSearchDirs
        )
        set(${parse_INCLUDE_DIRS} ${_configIncludeSearchDirs} PARENT_SCOPE)
    endif()
endfunction()
