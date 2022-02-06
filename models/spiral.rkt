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


(define (spiral mag [tail (sphere .25)])
  (union
   (sphere .25)
   (rotate-y (* 1. mag)
             (rotate-x 10.
                       (union
                        (move-x .125
                                (box .3 .19 .19))
                        (move-x .25
                                tail))))))


(define (descend fn steps [mag 0.])
  (if (steps . > . 1)
      (fn mag (descend fn (- steps 1) (+ mag 1.)))
      (fn mag)))


(define (spin steps tree)
  (let ([step (/ 360. steps)])
    (apply union
         (for/list ([i (in-range steps)])
           (rotate-z (* i step) tree)))))


(define (emit-glsl)
  (compile (spin 11 (descend spiral 70))))
