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

; CSGST = Constructive Solid Geometry Syntax Tree

(require racket/math)
(require math/flonum)
(require "vec.rkt")


(provide brush?
         blend-operator?
         binary-operator?
         operator?
         transform?
         csg?
         assert-csg
         splat
         brush-bounds
         align
         sphere
         ellipsoid
         box
         cube
         torus
         cylinder
         union
         diff
         inter
         blend
         move
         move-x
         move-y
         move-z
         rotate-x
         rotate-y
         rotate-z)


(define (brush? expr)
  (case (car expr)
    [(sphere
      ellipsoid
      box
      cube
      torus
      cylinder) #t]
    [else #f]))


(define (binary-operator? expr)
  (case (car expr)
    [(union
      diff
      inter) #t]
    [else #f]))


(define (blend-operator? expr)
  (case (car expr)
    [(blend-union
      blend-diff
      blend-inter) #t]
    [else #f]))


(define (operator? expr)
  (case (car expr)
    [(union
      diff
      inter
      blend-union
      blend-diff
      blend-inter) #t]
    [else #f]))


(define (transform? expr)
  (case (car expr)
    [(move
      move-x
      move-y
      move-z
      mat4
      quat) #t]
    [else #f]))


(define (csg? expr)
  (or (brush? expr)
      (operator? expr)
      (transform? expr)))


(define (assert-csg expr)
  (unless (csg? expr) (error "Expected CSG expression:" expr))
  (void))


(define (splat args)
  (apply values args))


; Returns a brush's AABB
(define (brush-bounds brush args)
  (case brush
    [(sphere)
     (let* ([high (apply vec3 args)]
            [low (vec* -1. high)])
       (list low high))]

    [(ellipsoid)
     (let* ([high (apply vec3 args)]
            [low (vec* -1. high)])
       (list low high))]

    [(box)
     (let* ([high (apply vec3 args)]
            [low (vec* -1. high)])
       (list low high))]

    [(torus)
     (let-values ([(major-radius minor-radius) (splat args)])
       (let* ([radius (+ major-radius minor-radius)]
              [high (vec3 radius radius minor-radius)]
              [low (vec* -1 high)])
         (list low high)))]

    [(cylinder)
     (let-values ([(radius extent) (splat args)])
       (let* ([high (vec3 radius radius extent)]
              [low (vec* -1 high)])
         (list low high)))]))


; Override a brush's origin.
(define (align x y z brush)
  (unless (brush? brush) (error "Expected CSG brush:" brush))
  (if (and (= x 0)
           (= y 0)
           (= z 0))
      brush
      (let*
          ([aabb (brush-bounds (car brush) (cdr brush))]
           [low (car aabb)]
           [high (cadr aabb)]
           [alpha (vec+ (vec* (vec3 x y z) .5) .5)]
           [anchor (lerp high low alpha)])
        (let-values ([(x y z) (splat anchor)])
          `(move ,x ,y ,z, brush)))))


(define (sphere diameter)
  (let ([radius (/ (fl diameter) 2.)])
    `(sphere ,radius)))


(define (ellipsoid
         diameter-x
         diameter-y
         diameter-z)
  (let ([radius-x (/ (fl diameter-x) 2.)]
        [radius-y (/ (fl diameter-y) 2.)]
        [radius-z (/ (fl diameter-z) 2.)])
    `(ellipsoid ,radius-x ,radius-y ,radius-z)))


(define (box width depth height)
  (let ([extent-x (/ (fl width) 2.)]
        [extent-y (/ (fl depth) 2.)]
        [extent-z (/ (fl height) 2.)])
    `(box ,extent-x ,extent-y ,extent-z)))


(define (cube size)
  (box size size size))


(define (torus major-diameter minor-diameter)
  (let* ([minor-radius (/ (fl minor-diameter) 2.)]
         [major-radius (- (/ (fl major-diameter) 2.) minor-radius)])
    `(torus ,major-radius ,minor-radius)))


(define (cylinder diameter height)
  (let ([radius (/ (fl diameter) 2.)]
        [extent (/ (fl height) 2.)])
    `(cylinder ,radius ,extent)))


(define (union lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (if (null? etc)
      `(union ,lhs ,rhs)
      (apply union (cons (union lhs rhs) etc))))


(define (diff lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (if (null? etc)
      `(diff ,lhs ,rhs)
      (apply diff (cons (diff lhs rhs) etc))))


(define (inter lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (if (null? etc)
      `(inter ,lhs ,rhs)
      (apply inter (cons (inter lhs rhs) etc))))


(define (blend operator threshold lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (define op (cond
               [(or (eq? operator union) (eq? operator 'union)) 'blend-union]
               [(or (eq? operator diff) (eq? operator 'diff)) 'blend-diff]
               [(or (eq? operator inter) (eq? operator inter)) 'blend-inter]
               [else (error "Invalid CSG operator:" operator)]))
  (define (inner lhs rhs . etc)
    (if (null? etc)
        `(,op ,(fl threshold) ,lhs ,rhs)
        (apply inner (cons (inner lhs rhs) etc))))
  (apply inner (append (list lhs rhs) etc)))


(define (move x y z child)
  (assert-csg child)
  `(move ,(fl x) ,(fl y) ,(fl z) ,child))


(define (move-x n child)
  (move n 0. 0. child))


(define (move-y n child)
  (move 0. n 0. child))


(define (move-z n child)
  (move 0. 0. n child))


(define (radians degrees)
  (fl (degrees->radians degrees)))


(define (rotate-x degrees child)
  (assert-csg child)
  (let* ([x 0.]
         [y 0.]
         [z 0.]
         [w 1.]
         [radians (/ (radians degrees) 2.0)]
         [s (sin radians)]
         [c (cos radians)])
    `(quat
      ,(+ (* x c) (* w s))
      ,(+ (* y c) (* z s))
      ,(- (* z c) (* y s))
      ,(- (* w c) (* x s))
      ,child)))


(define (rotate-y degrees child)
  (assert-csg child)
  (let* ([x 0.]
         [y 0.]
         [z 0.]
         [w 1.]
         [radians (/ (radians degrees) 2.0)]
         [s (sin radians)]
         [c (cos radians)])
    `(quat
       ,(- (* x c) (* z s))
       ,(+ (* y c) (* w s))
       ,(+ (* z c) (* x s))
       ,(- (* w c) (* y s))
       ,child)))


(define (rotate-z degrees child)
  (assert-csg child)
  (let* ([x 0.]
         [y 0.]
         [z 0.]
         [w 1.]
         [radians (/ (radians degrees) 2.0)]
         [s (sin radians)]
         [c (cos radians)])
    `(quat
      ,(+ (* x c) (* y s))
      ,(- (* y c) (* x s))
      ,(+ (* z c) (* w s))
      ,(- (* w c) (* z s))
      ,child)))
