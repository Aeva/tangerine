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

(provide profile-scope)


(define-ffi-definer define-backend (ffi-lib #f))


(define-backend BeginRacketEvent (_fun _string/utf-8 -> _void)
  #:fail (λ () (λ (name) (void))))

(define-backend EndRacketEvent (_fun -> _void)
  #:fail (λ () (λ () (void))))


(define-syntax profile-scope
  (syntax-rules ()
    [(profile-scope name exprs ...)
     (begin0
       (begin
         (BeginRacketEvent name)
         exprs ...)
       (EndRacketEvent))]))
