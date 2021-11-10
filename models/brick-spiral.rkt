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


(require tangerine)
(require tangerine/vec)
(require "bricks.rkt")
(provide emit-glsl)


; Spiral thing with a shape cut out of it.
(define (emit-glsl)
  (compile
   (diff
    (brick-walk
     30
     (vec2 0 0)
     (vec2 2 0)
     (vec2 2 2)
     (vec2 -2 2)
     (vec2 -2 -2)
     (vec2 4 -2)
     (vec2 4 4)
     (vec2 -4 4)
     (vec2 -4 -4)
     (vec2 6 -4)
     (vec2 6 6)
     (vec2 -6 6)
     (vec2 -6 -6)
     (vec2 6 -6)
     (vec2 6 -5)
     (vec2 -5 -5)
     (vec2 -5 5)
     (vec2 5 5)
     (vec2 5 -3)
     (vec2 -3 -3)
     (vec2 -3 3)
     (vec2 3 3)
     (vec2 3 -1)
     (vec2 -1 -1)
     (vec2 -1 1)
     (vec2 0 1)
     (vec2 0 0))
    (move -8 -8 -0.1
    (align -1 -1 -1 (sphere 10))))))
