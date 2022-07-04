#lang info

(define pkg-name "tangerine-x86_64-win32")
(define collection "tangerine")
(define version "0.0")
(define pkg-desc
  "native libraries for \"tangerine\" on \"x86_64-win32\"")
(define pkg-authors
  '(aeva))
(define license
  'Apache-2.0)

(define install-platform
  "win32\\x86_64")
(define copy-foreign-libs
  '("tangerine.dll"))

(define deps '("base"))
