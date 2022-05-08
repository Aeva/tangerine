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


(require racket/contract)
(require "csgst.rkt")


; Stuff from Racket.
(provide
 λ if < = > begin + - * / quote
 #%app #%top #%datum
 (rename-out [module-begin #%module-begin]))


(define/contract (provide-model first . rest)
  ((csg?) () #:rest (listof csg?) . ->* . csg?)
  (if (null? rest)
      first
      (apply union (cons first rest))))
   

; Automatically provide the program expression as 'model'.
(define-syntax-rule (module-begin expr ...)
  (#%module-begin
   (define model (provide-model expr ...))
   (provide model)))


; Stuff from Tangerine.  These should be limited to simple validated transformations on the input data.
(provide
 align
 paint
 sphere
 ellipsoid
 box
 cube
 torus
 cylinder
 plane
 union
 diff
 inter
 blend
 move
 move-x
 move-y
 move-z
 rotate-x
 rotate-y
 rotate-z)
