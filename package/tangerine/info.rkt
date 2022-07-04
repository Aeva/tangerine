#lang info

(define pkg-name "tangerine")
(define collection "tangerine")
(define name "Tangerine")
(define version "0.0")
(define pkg-desc
  "A library for constructing and executing signed distance functions")
(define pkg-authors
  '(aeva))
(define license
  'Apache-2.0)

(define deps
  '("base"
    "sandbox-lib"
    "vec"
    ["tangerine-x86_64-linux" #:platform #rx"^x86_64-linux(?:-natipkg)?$"]
    ["tangerine-x86_64-win32" #:platform "win32\\x86_64"]))
