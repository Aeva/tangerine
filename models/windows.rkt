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

(require tangerine)
(require tangerine/vec)
(require "bricks.rkt")
(provide emit-glsl)


(define box-test
  (diff
   (cube 2.3)
   (sphere 3.0)))


(define (window-negative x)
  (move x 0. 5. (box 2.1 1.1 4.1)))


(define (window-positive x alpha)
  (define (sash y z)
    (move 0. y z
          (union
           (union
            (diff
             (box 2. 0.125 2.)
             (box 1.8 .5 1.8))
            (box 0.05125 0.05125 2.))
           (box 2.0 0.05125 0.05125))))
  (move x 0. 5.
          (union
           (union
            (union
             (sash 0.05125 1.0)
             (sash -0.05125
                   (+ (* -1.0 (- 1.0 alpha))
                      (* 0.8 alpha))))
            (move-z -2.05
                    (box 2.6 1.0 0.1)))
           (diff
            (move-z -.1
                    (box 2.4 .7 4.6))
            (box 2. 1. 4.)))))


(define (wall . params)
  (define tree
    (brick-walk
     48
     (vec2 -5 0)
     (vec2 5 0)))
  (for ([param params])
    (let* ([param-x (car param)]
           [shape (window-negative param-x)])
      (set! tree
            (diff tree
                  shape))))
  (for ([param params])
    (let* ([param-x (car param)]
           [param-a (cadr param)]
           [shape (window-positive param-x param-a)])
      (set! tree
            (union tree
                   shape))))
  tree)


(define normal-wall (wall '(-3.0 0.0)
                          '(0.0 0.3)
                          '(3.0 0.8)))


(define (emit-glsl)
  (compile (paint 'silver normal-wall)))
