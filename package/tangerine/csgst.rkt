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
(require racket/list)
(require racket/match)
(require math/flonum)
(require "vec.rkt")
(require "color-names.rkt")


(provide brush?
         blend-operator?
         binary-operator?
         operator?
         transform?
         paint?
         csg?
         assert-csg
         splat
         brush-bounds
         align
         paint
         paint-over
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


; Returns #t if the expression is a CSG brush shape.
(define (brush? expr)
  (case (car expr)
    [(sphere
      ellipsoid
      box
      cube
      torus
      cylinder) #t]
    [else #f]))


; Returns #t if the expression is a simple CSG operator, like diff.
(define (binary-operator? expr)
  (case (car expr)
    [(union
      diff
      inter) #t]
    [else #f]))


; Returns #t if the expression is a CSG blend operator.
(define (blend-operator? expr)
  (case (car expr)
    [(blend-union
      blend-diff
      blend-inter) #t]
    [else #f]))


; Returns #t if the expression is a CSG operator.
(define (operator? expr)
  (case (car expr)
    [(union
      diff
      inter
      blend-union
      blend-diff
      blend-inter) #t]
    [else #f]))


; Returns #t if the expression is a CSG transform.
(define (transform? expr)
  (case (car expr)
    [(move
      move-x
      move-y
      move-z
      align
      mat4
      quat) #t]
    [else #f]))


; Returns #t if the expression is a CSG material annotation.
(define (paint? expr)
  (eq? (car expr) 'paint))


; Returns #t if the expression is a CSG expression.
(define (csg? expr)
  (or (paint? expr)
      (brush? expr)
      (operator? expr)
      (transform? expr)))


; Raise an error if the provided expresion is not a valid CSG expression.
(define (assert-csg expr)
  (unless (csg? expr) (error "Expected CSG expression:" expr))
  (void))


; Decompose a list into values.
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


; Move a csgst tree relative to the origin.  The x y and z values are
; expected to be a number between -1 and 1, where -1 -1 -1 places the
; tree such that the lowest corner of the inner bounding box is aligned
; with the origin, and 1 1 1 places the tree such that the highest corner
; of the inner bounding box is aligned with the origin.
(define (align x y z csgst)
  (unless (csg? csgst) (error "Expected CSG expression:" csgst))
  `(align ,(fl x) ,(fl y) ,(fl z) ,csgst))


; Provides the common functionality to paint and paint-over.
(define (paint-propagate red green blue mode csgst)
  (cond [(brush? csgst)
         `(paint ,red ,green ,blue ,csgst)]

        [(paint? csgst)
         (if mode
             `(paint ,red ,green ,blue ,(cddr csgst))
             csgst)]

        [(binary-operator? csgst)
         (cons (car csgst)
               (for/list ([subtree (in-list (cdr csgst))])
                 (paint-propagate red green blue mode subtree)))]

        [(blend-operator? csgst)
         (let ([instruction (car csgst)]
               [threshold (cadr csgst)]
               [operands (cddr csgst)])
           (cons instruction
                 (cons threshold
                       (for/list ([subtree (in-list operands)])
                         (paint-propagate red green blue mode subtree)))))]

        [(transform? csgst)
         (let* ([pivot (- (length csgst) 1)]
                [transform (take csgst pivot)]
                [subtree (last csgst)])
           (append transform (list (paint-propagate red green blue mode subtree))))]

        [else (error "unreachable")]))


; Extract the paint color, and then propogate the paint attribute in accordance to the mode.
; Exposed to the user via functions paint and paint-over.
(define (paint-inner mode params)

  (define (scrub number)
    (unless (number? number) (error "Expected number, got: " number))
    (max (min (fl number) 255.0) 0.0))

  (let*-values
      ([(csgst) (values (last params))]

       [(red green blue)
        (match params
          [(list red green blue _)
           (values red green blue)]
          [(list name _) (color-by-name name)])])

    (assert-csg csgst)

    (paint-propagate (scrub red) (scrub green) (scrub blue) mode csgst)))


; Paint all brushes in the subtree with a material annotation.  This
; will only modify brushes that do not already have an annotation.
(define (paint . params)
  (paint-inner #f params))


; Paint all brushes in the subtree with a material annotation.
; This will override all annotations further down the tree.
(define (paint-over . params)
  (paint-inner #t params))


; Spherice brush shape.
(define (sphere diameter)
  (let ([radius (/ (fl diameter) 2.)])
    `(sphere ,radius)))


; Ellipsoid brush shape.
(define (ellipsoid
         diameter-x
         diameter-y
         diameter-z)
  (let ([radius-x (/ (fl diameter-x) 2.)]
        [radius-y (/ (fl diameter-y) 2.)]
        [radius-z (/ (fl diameter-z) 2.)])
    `(ellipsoid ,radius-x ,radius-y ,radius-z)))


; Box brush shape.
(define (box width depth height)
  (let ([extent-x (/ (fl width) 2.)]
        [extent-y (/ (fl depth) 2.)]
        [extent-z (/ (fl height) 2.)])
    `(box ,extent-x ,extent-y ,extent-z)))


; Cube brush shape.
(define (cube size)
  (box size size size))


; Torus brush shape.
(define (torus major-diameter minor-diameter)
  (let* ([minor-radius (/ (fl minor-diameter) 2.)]
         [major-radius (- (/ (fl major-diameter) 2.) minor-radius)])
    `(torus ,major-radius ,minor-radius)))


; Cylinder brush shape.
(define (cylinder diameter height)
  (let ([radius (/ (fl diameter) 2.)]
        [extent (/ (fl height) 2.)])
    `(cylinder ,radius ,extent)))


; Union CSG operator.
(define (union lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (if (null? etc)
      `(union ,lhs ,rhs)
      (apply union (cons (union lhs rhs) etc))))


; Subtraction CSG operator.
(define (diff lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (if (null? etc)
      `(diff ,lhs ,rhs)
      (apply diff (cons (diff lhs rhs) etc))))


; Intersection CSG operator.
(define (inter lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (if (null? etc)
      `(inter ,lhs ,rhs)
      (apply inter (cons (inter lhs rhs) etc))))


; Blending CSG operator.
(define (blend operator threshold lhs rhs . etc)
  (assert-csg lhs)
  (assert-csg rhs)
  (define op (cond
               [(or (eq? operator union) (eq? operator 'union)) 'blend-union]
               [(or (eq? operator diff) (eq? operator 'diff)) 'blend-diff]
               [(or (eq? operator inter) (eq? operator 'inter)) 'blend-inter]
               [else (error "Invalid CSG operator:" operator)]))
  (define (inner lhs rhs . etc)
    (if (null? etc)
        `(,op ,(fl threshold) ,lhs ,rhs)
        (apply inner (cons (inner lhs rhs) etc))))
  (apply inner (append (list lhs rhs) etc)))


; Translation transform.
(define (move x y z child)
  (assert-csg child)
  `(move ,(fl x) ,(fl y) ,(fl z) ,child))


; Translation transform.
(define (move-x n child)
  (move n 0. 0. child))


; Translation transform.
(define (move-y n child)
  (move 0. n 0. child))


; Translation transform.
(define (move-z n child)
  (move 0. 0. n child))


; Convert from degrees to radians.
(define (radians degrees)
  (fl (degrees->radians degrees)))


; Rotation transform.
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


; Rotation transform.
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


; Rotation transform.
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
