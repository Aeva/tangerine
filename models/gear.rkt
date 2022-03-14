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

(require racket/math)
(require tangerine)
(provide emit-glsl)


(define (gear diameter height teeth)
  (let* ([radius (/ diameter 2)]
         [circumference (* 2 pi radius)]
         [slice (/ circumference teeth)]
         [step (/ 360 teeth)]
         [inner-diameter (- diameter (* slice 3))])
    (diff
     (cylinder diameter height)
     (move-z (/ height 2) (cylinder inner-diameter (/ height 3)))
     (move-z (/ height -2) (cylinder inner-diameter (/ height 3)))
     (apply union
            (for/list ([t (in-range 4)])
              (rotate-z (* t 90)
                        (move-x (/ radius 2) (cylinder (/ diameter 4) (+ height .1))))))
     (apply union
            (for/list ([t (in-range teeth)])
              (rotate-z (* t step)
                        (move-x radius
                                (rotate-z 45
                                          (box slice slice (+ height .1))))))))))


(define (emit-glsl)
  (compile (gear 8 .5 100)))
