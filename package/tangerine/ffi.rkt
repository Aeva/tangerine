#lang racket/base

; Copyright 2022 Aeva Palecek
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;     http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

(require ffi/unsafe)
(require ffi/unsafe/define)
(require racket/runtime-path)

(provide define-backend)

(define-runtime-path tangerine-dll "tangerine.dll")
(define-runtime-path tangerine-so "tangerine.so")

(define-ffi-definer
  define-backend
  (begin
    (ffi-lib (case (system-type)
               [(windows) tangerine-dll]
               [(unix) tangerine-so])
             #:fail (λ () (void)))
    (ffi-lib #f))
  #:default-make-fail make-not-available)
