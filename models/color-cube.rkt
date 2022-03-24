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
(require tangerine/eval)
(provide emit-glsl)


(define (color-cube [extent 10])
  (apply union
         (for*/list ([z (in-range extent)]
                     [y (in-range extent)]
                     [x (in-range extent)])
           (move x y z
                 (paint (/ (+ x 1) extent)
                        (/ (+ y 1) extent)
                        (/ (+ z 1) extent)
                        (align -1 -1 -1
                               (sphere .6)))))))


(define (emit-glsl)
  (compile (color-cube)))
