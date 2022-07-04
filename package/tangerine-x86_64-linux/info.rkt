#lang info

(define pkg-name "tangerine-x86_64-linux")
(define collection "tangerine")
(define version "0.0")
(define pkg-desc
  "native libraries for \"tangerine\" on \"x86_64-linux\"")
(define pkg-authors
  '(aeva))
(define license
  'Apache-2.0)

(define install-platform
  #rx"^x86_64-linux(?:-natipkg)?$")
(define copy-foreign-libs
  '("tangerine.so"))

(define deps '("base"))
