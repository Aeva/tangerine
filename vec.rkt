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


(require racket/list)


(provide swiz
         vec4
         vec3
         vec2
         vec4?
         vec3?
         vec2?
         vec+
         vec-
         vec*
         vec/
         vec-min
         vec-max
         vec-len
         normalize
         quat-rotate
         rcp)


(define (vec size . params)
  (if (and
       (number? (car params))
       (null? (cdr params)))
      (for/list ([i (in-range size)])
        (car params))
      (let ([out null]
            [params (append (flatten params) (list 0. 0. 0. 0.))])
        (take params size))))


(define (vec4 . params)
  (apply vec (cons 4 params)))


(define (vec3 . params)
  (apply vec (cons 3 params)))


(define (vec2 . params)
  (apply vec (cons 2 params)))


(define (vec4? thing)
  (and (list? thing)
       (eq? 4 (length thing))
       (number? (car thing))
       (number? (cadr thing))
       (number? (caddr thing))
       (number? (cadddr thing))))


(define (vec3? thing)
  (and (list? thing)
       (eq? 3 (length thing))
       (number? (car thing))
       (number? (cadr thing))
       (number? (caddr thing))))


(define (vec2? thing)
  (and (list? thing)
       (eq? 2 (length thing))
       (number? (car thing))
       (number? (cadr thing))))


(define (swiz vec . channels)
  (for/list ([c (in-list channels)])
    (list-ref vec c)))


(define (vec-op op)
  (define (outter lhs rhs . others)
    (let ([inner
           (λ (lhs rhs)
             (cond [(number? lhs)
                    (for/list ([num (in-list rhs)])
                      (op lhs num))]
                   [(number? rhs)
                    (for/list ([num (in-list lhs)])
                      (op num rhs))]
                   [else
                    (for/list ([i (in-range (min (length lhs) (length rhs)))])
                      (let ([a (list-ref lhs i)]
                            [b (list-ref rhs i)])
                        (op a b)))]))])
      (let ([ret (inner lhs rhs)])
        (if (null? others)
            ret
            (apply outter (cons ret others))))))
  outter)


(define vec+ (vec-op +))


(define vec- (vec-op -))


(define vec* (vec-op *))


(define vec/ (vec-op /))


(define vec-min (vec-op min))


(define vec-max (vec-op max))


(define (dot lhs rhs)
  (apply + (vec* lhs rhs)))


(define (vec-len vec)
  (sqrt (dot vec vec)))


(define (normalize vec)
  (vec/ vec (dot vec vec)))


(define (quat-rotate point quat)
  (let* ([sign (vec2 1. -1.)]
         [tmp (vec4
               (dot point (vec* (swiz sign 0 1 0) (swiz quat 3 2 1)))
               (dot point (vec* (swiz sign 0 0 1) (swiz quat 2 3 0)))
               (dot point (vec* (swiz sign 1 0 0) (swiz quat 1 0 3)))
               (dot point (vec* (swiz sign 1 1 1) (swiz quat 0 1 2))))])
    (vec3
     (dot tmp (vec* (swiz sign 0 1 0 1) (swiz quat 3 2 1 0)))
     (dot tmp (vec* (swiz sign 0 0 1 1) (swiz quat 2 3 0 1)))
     (dot tmp (vec* (swiz sign 1 0 0 1) (swiz quat 1 0 3 2))))))


(define (rcp vec)
  (if (number? vec)
      (/ 1.0 vec)
      (vec/ 1.0 vec)))
