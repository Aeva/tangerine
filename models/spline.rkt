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
(require vec)
(require racket/list)
(require racket/format)
(provide emit-glsl)


; Helper macro for the "bezier" macro defined below.
; Not intended to be used directly.
(define-syntax bezier-reduce
  (syntax-rules ()
    ; If there are exactly two expressions accumulated, produce a lerp.
    [(bezier-reduce (t pt1 pt2)) (lerp pt1 pt2 t)]
    
    ; If there are more than two expressions accumulated, recurse.
    [(bezier-reduce (t pt1 pt2 ptn ...)) (bezier-reduce (t) pt1 pt2 ptn ...)]

    ; Combine pending expressions via lerping and accumulate the results.
    [(bezier-reduce (t acc ...) pt1 pt2) (bezier-reduce (t acc ... (lerp pt1 pt2 t)))]
    [(bezier-reduce (t acc ...) pt1 pt2 ptn ...) (bezier-reduce (t acc ... (lerp pt1 pt2 t)) pt2 ptn ...)]))


; Create a n-dimensional bezier curve function.
(define-syntax bezier
  (syntax-rules ()
    [(bezier points ...) (λ (t) (bezier-reduce (t) points ...))]))


; For the given curve function, spacing, and optional start and
; stop time values, find a t-value for the next step along the curve.
(define (narrow curve spacing [start 0.] [stop 1.])
  (let* ([pt1 (curve start)]
         [pt2 (curve stop)]
         [dist (distance pt1 pt2)])
    (if (dist . <= . spacing)
        stop
        (narrow curve spacing start (lerp start stop 0.9)))))


; Produce a set of points that are roughly the provided spacing apart
; along the given curve function.  The results include the endpoints.
(define (walk curve spacing)

  ; Starting from one end of the curve, repeatedly call narrow to produce
  ; a set of points that are roughly the specified spacing apart.
  (define (inner curve spacing [start 0.] [stop 1.] [visited (list start)])
    (let ([next (narrow curve spacing start stop)])
      (if (= next stop)
          visited
          (inner curve spacing next stop (append visited (list next))))))

  ; Walk forwards and backwards along the curve, and then lerp the results
  ; so that the data lines up perfectly at both endpoints.
  (let* ([forward (inner curve spacing)]
         [backward (reverse (inner curve spacing 1. 0.))]
         [scale (- (min (length forward) (length backward)) 1)])
    (for/list ([lhs (in-list forward)]
               [rhs (in-list backward)]
               [i (in-naturals 0)])
      (let ([alpha (/ i scale)])
        (curve (lerp lhs rhs alpha))))))


; Stamp the provided brush along the curve.
(define (stamp brush spacing curve)

  (define (inner lhs rhs . rest)
    (let ([midpoint (lerp lhs rhs 0.5)])
      (if (null? rest)
          (list midpoint)
          (cons midpoint
                (apply inner (cons rhs rest))))))

  (let* ([stops (walk curve spacing)]
         [points (apply inner stops)])
    (apply union
           (for/list ([point (in-list points)])
             (apply move (append point (list brush)))))))


(define (emit-glsl)
  (compile
   (stamp (sphere .2)
          .1
          (bezier
           (vec3 -2 0 0)
           (vec3 0 4 0)
           (vec3 0 -4 0)
           (vec3 2 0 0)))))
