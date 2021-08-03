#lang racket/base

; Copyright 2021 Aeva Palecek
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

(require "sdf.rkt")

(provide emit-glsl)

(define simple-scene
  (union
    (smooth-cut
     0.5
     (sphere 1.8)
     (trans .9 -.5 0.
            (sphere 1.6)))
    (trans .9 -.5 0.
            (sphere 0.8))))

(define box-test
  (cut
   (box 1.3 0.25 1.3)
   (sphere 1.3)))

(define (emit-glsl)
  (scene box-test))
