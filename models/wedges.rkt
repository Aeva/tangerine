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


(define wedge
  (move-x -2.
          (inter
           (plane .125 1 0)
           (plane .125 -1 0))))


(define (slices count)
  (let ([step (/ 360 count)])
    (apply union
           (for/list ([t (in-range count)])
             (rotate-z (* t step) wedge)))))


(define thingy
  (align 0 0 -1
         (inter
          (union
           (diff (sphere 20)
                 (slices 30)
                 (move-z 2
                         (plane .1 .5 -1))
                 (move 0 -2 5.5
                       (sphere 15)))
           (move 0 -1 -1.2
                 (sphere 2)))
          (move-z -2
                  (plane 0 0 -1)))))


(define (emit-glsl)
  (compile thingy))
