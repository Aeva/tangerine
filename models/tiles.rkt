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
(provide emit-glsl)

(define (bezier brush steps ctrl-a ctrl-b ctrl-c)
  (define (step t)
    (apply move
           (append
            (lerp
             (lerp ctrl-a ctrl-b t)
             (lerp ctrl-b ctrl-c t)
             t)
            (list brush))))
  (apply union
         (for/list ([i (in-range (+ 1 steps))])
           (let ([a (/ i steps)])
             (step a)))))


(define (terminus)
  (if (> (random) 0.5)
      (vec3 (- (* (random) 2.) 1.) 1. 0.)
      (vec3 1. (- (* (random) 2.) 1.) 0.)))


(define (midpoint)
  (vec- (vec* (vec3 (random) (random) 0.0) (vec3 2 2 0)) (vec3 1 1 0)))


(define tile-base
  (diff
   (blend union .05
          (move-z -.125 (box 1.9 1.9 .25))
          (move-z -.275 (box 2 2 .45)))
   (rotate-z 45
             (move-z .075
                     (box 1.325 1.325 .2)))))

(define (spline-test)
  (let ([start (terminus)]
        [stop (vec* (terminus) -1.)]
        [middle (midpoint)])
    (diff
     tile-base
     (bezier (cube .075) 50 start stop middle))))


(define brick
  (move 0 .2 .125
        (box .95 .4 .25)))


(define half-brick-r
  (move .265 .2 .125
        (box .475 .4 .25)))


(define half-brick-l
  (move -.265 .2 .125
        (box .475 .4 .25)))


(define brick-segment
  (union
   (move-x -.5 brick)
   (move-x .5 brick)
   (move-z .3
           (union
            brick
            (move-x -.5 half-brick-l)
            (move-x .5 half-brick-r)))))


(define brick-wall
       (union
        (move 0 .25 -.25
         (box 2 .5 .5))
        brick-segment
        (move-z .6 brick-segment)
        (move-z 1.2 brick-segment)
        (move-z 1.8 brick-segment)
        (move-z 2.4 brick-segment)
        (move-z 3.0 brick-segment)
        (move 0 .2 1.75
              (box 2 .3 3.5))))


(define wall-join
  (move-z (- (/ 4.05 2) .5)
          (box .5 .5 4.05)))


(define wall-n
  (move-y 1 brick-wall))


(define wall-ne
  (move 1.25 1.25 0 wall-join))


(define wall-e
  (move-x 1 (rotate-z -90 brick-wall)))


(define wall-se
  (move 1.25 -1.25 0 wall-join))


(define wall-s
  (move-y -1 (rotate-z 180 brick-wall)))


(define wall-sw
  (move -1.25 -1.25 0 wall-join))


(define wall-w
  (move-x -1 (rotate-z 90 brick-wall)))


(define wall-nw
  (move -1.25 1.25 0 wall-join))



(define inner-corners
  (union
   tile-base
   wall-n
   wall-ne
   wall-e
   wall-se
   wall-s
   wall-sw
   wall-w
   wall-nw
   ))


(define path-test
  (union
   (union
    tile-base
    wall-n
    wall-nw
    wall-w)
   (move-y -2
           (union
            tile-base
            wall-s
            wall-sw
            wall-w))
   (move-x 2
           (union
            tile-base
            wall-n))
   (move-x 4 tile-base)
   (move 4 2 0
         (union
          wall-w
          tile-base))
   (move 4 4 0
         (union wall-w tile-base))))




(define (emit-glsl)
  (compile
   path-test))
