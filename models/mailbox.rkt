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
(provide emit-glsl)


(define mailbox-base
  (union
   (move-z 2 (rotate-y 90 (cylinder 4 4)))
   (cube 4)))


(define frame-bend
  (union
   (move-z 3
           (inter
            (rotate-y 90
                      (torus 4 .15))
            (move-z 2
                    (box 1 4 4))))
   (move-y 1.925 (cylinder .15 6))
   (move-y -1.925 (cylinder .15 6))))


(define handle
  (move 0 -.2 4.45
        (union
         (move-y .25
                 (rotate-y 90
                           (cylinder .05 1)))
         (move-x .5
                 (rotate-x 90 (cylinder .05 .5)))
         (move-x -.5
                 (rotate-x 90 (cylinder .05 .5)))
         (move .5 .25 0 (sphere .05))
         (move -.5 .25 0 (sphere .05)))))


(define legs
  (move-z -1.875
           (diff
            (box 4 3.85 2.25)
            (box 3.5 4 2.26)
            (box 4.1 3.25 2.26)
            (box 3.9 3.75 2.26))))


(define mailbox
  (paint #x01349d
         (union
          (diff
           (union
            (move-x -2 frame-bend)
            (move-x 2 frame-bend)
            (move-z 3 (rotate-y 90 (cylinder 3.85 4)))
            (move-z 1.15 (box 4 3.85 3.85)))
           (union
            (move-z 3 (rotate-x 80 (move 0 2 -1 (box 3.6 2 2))))
            (move 0 2 3.3 (box 3.6 3.4 1))
            (move-z 3 (rotate-y 90 (cylinder 3.8 3.8)))))
          (move 0 0.75 3.2 (rotate-x -18 (box 3.85 2.5 .05)))
          (move 0 -.2 3.65 (box 3.85 .05 2.5))
          (move 0 -.15 4.15 (box 3.5 0.05 1.25))
          (move 0 -.15 3.5 (rotate-y 90 (cylinder .1 3.5)))
          legs handle)))


(define (emit-glsl)
  (compile mailbox))