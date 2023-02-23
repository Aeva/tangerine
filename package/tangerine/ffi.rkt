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
(require setup/dirs)
(require (for-syntax racket/base))
(require racket/runtime-path)

(provide define-backend)

(define-runtime-path tangerine-dll
  '(so "tangerine"))

(define-ffi-definer define-backend
  (let* ([fail (λ () #f)])
    ; If the symbols are not already available to the process, attempt to load tangerine.dll,
    ; wherever it may be.  If Racket is embeded in Tangerine, the library
    ; won't be there, so fail gracefully.
    (unless (ffi-obj-ref "EvalTree" (ffi-lib #f) fail)
      (ffi-lib tangerine-dll #:fail fail))

    ; Then return the ffi-lib for the entire process either way.  If loading the dll was
    ; required (and successful), this will include those foreign symbols, along with
    ; everything else accessible to the process.
    (ffi-lib #f))
  #:default-make-fail make-not-available)
