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


(define box-test
  (cut
    (cube 2.3)
    (sphere 3.0)))


(define (window-negative x)
  (trans-x x (box 2. 1. 4.)))


(define (window-positive x alpha)
  (define (sash y z)
    (trans 0. y z
           (union
            (union
             (cut
              (box 2. 0.125 2.)
              (box 1.8 .5 1.8))
             (box 0.05125 0.05125 2.))
            (box 2.0 0.05125 0.05125))))
  (trans-x x
           (union
            (union
             (union
              (sash 0.05125 1.0)
              (sash -0.05125
                    (+ (* -1.0 (- 1.0 alpha))
                       (* 0.8 alpha))))
             (trans-z -2.05
                      (box 2.6 1.0 0.1)))
            (cut
             (trans-z -.1
                      (box 2.4 .7 4.6))
             (box 2. 1. 4.)))))


(define (wall width . params)
  (define tree
    (box width 0.5 10.0))
  (for ([param params])
    (let* ([param-x (car param)]
           [shape (window-negative param-x)])
      (set! tree
            (cut tree
                 shape))))
  (for ([param params])
    (let* ([param-x (car param)]
           [param-a (cadr param)]
           [shape (window-positive param-x param-a)])
      (set! tree
            (union tree
                   shape))))
  tree)


(define (emit-glsl)
  (scene (wall 10.0
               '(-3.0 0.0)
               '(0.0 0.3)
               '(3.0 0.8))))
