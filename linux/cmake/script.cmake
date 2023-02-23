#!/usr/bin/env -S guix shell cmake -- cmake -P

cmake_minimum_required(VERSION 3.24)

include(RacketParseConfigFile.cmake)

racket_parse_config_file("/gnu/store/al90nb7vrjhgbzr71x037c7ny73b03sb-tangerine-racket-layer-8.7/etc/racket/config.rktd"
    NAME _installationName
    LIB_DIRS _configLibDirs
    INCLUDE_DIRS _configIncludeDirs
)

message(NOTICE "Got:\n${_installationName}")
message(WARNING "Got:\n${_configLibDirs}")
message(NOTICE "Got:\n${_configIncludeDirs}")
