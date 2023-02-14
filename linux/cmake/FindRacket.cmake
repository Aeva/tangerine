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

find_library(Racket_links_file
    links #"links.rktd"
    PATH_SUFFIXES "racket")
#[[
find_path(Racket_CONFIG_DIR
  config.rktd
  PATH_SUFFIXES ../etc/racket ../etc
)
]]
#message(WARNING "${ENV:CMAKE_PREFIX_PATH}")
message(WARNING "${Racket_links_file}")
#[[find_library(Racket_LIBRARY
  NAMES racketcs libracketcs
  PATHS ${PC_Foo_LIBRARY_DIRS}
)
#]]
find_package_handle_standard_args(Racket DEFAULT_MSG Racket_links_file)
message(FATAL_ERROR "stopping here")
