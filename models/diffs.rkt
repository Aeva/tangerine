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
(provide emit-glsl)


(define test
  (diff
   (apply diff (cons
                (rotate-x 90 (cylinder 15 3.5))
                (for/list ([i (in-range 20)])
                  (rotate-y (* i 18)
                            (move-x 5 (box 4 4 .5))))))
   (diff
    (union
     (move-y 1.5
             (rotate-x 90 (cylinder 14.5 2)))
     (move-y -1.5
             (rotate-x 90 (cylinder 14.5 2))))
    (sphere 8))
   (rotate-x 90 (cylinder 4 5))))


(define (emit-glsl)
  (compile test))
