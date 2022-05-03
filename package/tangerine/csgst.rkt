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
(require racket/contract)
(require math/flonum)
(require "vec.rkt")
(require "color-names.rkt")


(provide brush?
         unbound?
         shape?
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
         plane
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


; Returns #t if the expression is an unbound CSG shape.
(define (unbound? expr)
  (case (car expr)
    [(plane) #t]
    [else #f]))


; Returns #t if the expression is a CSG shape.
(define (shape? expr)
  (or (brush? expr) (unbound? expr)))


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
  (and (list? expr)
       (or (paint? expr)
           (shape? expr)
           (operator? expr)
           (transform? expr))))


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
(define/contract (align x y z csgst)
  (number? number? number? csg? . -> . (list/c 'align flonum? flonum? flonum? csg?))
  `(align ,(fl x) ,(fl y) ,(fl z) ,csgst))


; Provides the common functionality to paint and paint-over.
(define (paint-propagate red green blue mode csgst)
  (cond [(shape? csgst)
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

  (define/contract (scrub number)
    (number? . -> . number?)
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
(define/contract (paint . params)
  (() #:rest (or/c (list/c number? number? number? csg?)
                   (list/c integer? csg?)
                   (list/c color-name? csg?)) . ->* . csg?)
  (paint-inner #f params))


; Paint all brushes in the subtree with a material annotation.
; This will override all annotations further down the tree.
(define/contract (paint-over . params)
    (() #:rest (or/c (list/c number? number? number? csg?)
                     (list/c integer? csg?)
                     (list/c color-name? csg?)) . ->* . csg?)
  (paint-inner #t params))


; Spherice brush shape.
(define/contract (sphere diameter)
  (number? . -> . (list/c 'sphere flonum?))
  (let ([radius (/ (fl diameter) 2.)])
    `(sphere ,radius)))


; Ellipsoid brush shape.
(define/contract (ellipsoid diameter-x diameter-y diameter-z)
  (number? number? number? . -> . (list/c 'ellipsoid flonum? flonum? flonum?))
  (let ([radius-x (/ (fl diameter-x) 2.)]
        [radius-y (/ (fl diameter-y) 2.)]
        [radius-z (/ (fl diameter-z) 2.)])
    `(ellipsoid ,radius-x ,radius-y ,radius-z)))


; Box brush shape.
(define/contract (box width depth height)
  (number? number? number? . -> . (list/c 'box flonum? flonum? flonum?))
  (let ([extent-x (/ (fl width) 2.)]
        [extent-y (/ (fl depth) 2.)]
        [extent-z (/ (fl height) 2.)])
    `(box ,extent-x ,extent-y ,extent-z)))


; Cube brush shape.
(define/contract (cube size)
  (number? . -> . (list/c 'box flonum? flonum? flonum?))
  (box size size size))


; Torus brush shape.
(define/contract (torus major-diameter minor-diameter)
  (number? number? . -> . (list/c 'torus flonum? flonum?))
  (let* ([minor-radius (/ (fl minor-diameter) 2.)]
         [major-radius (- (/ (fl major-diameter) 2.) minor-radius)])
    `(torus ,major-radius ,minor-radius)))


; Cylinder brush shape.
(define/contract (cylinder diameter height)
  (number? number? . -> . (list/c 'cylinder flonum? flonum?))
  (let ([radius (/ (fl diameter) 2.)]
        [extent (/ (fl height) 2.)])
    `(cylinder ,radius ,extent)))


; Plane unbound shape.
(define/contract (plane normal-x normal-y normal-z)
  (number? number? number? . -> . (list/c 'plane flonum? flonum? flonum?))
  `(plane ,(fl normal-x) ,(fl normal-y) ,(fl normal-z)))


; Union CSG operator.
(define/contract (union lhs rhs . etc)
  ((csg? csg?) () #:rest (listof csg?) . ->* . (list/c 'union csg? csg?))
  (if (null? etc)
      `(union ,lhs ,rhs)
      (apply union (cons (union lhs rhs) etc))))


; Subtraction CSG operator.
(define/contract (diff lhs rhs . etc)
  ((csg? csg?) () #:rest (listof csg?) . ->* . (list/c 'diff csg? csg?))
  (if (null? etc)
      `(diff ,lhs ,rhs)
      (apply diff (cons (diff lhs rhs) etc))))


; Intersection CSG operator.
(define/contract (inter lhs rhs . etc)
  ((csg? csg?) () #:rest (listof csg?) . ->* . (list/c 'inter csg? csg?))
  (if (null? etc)
      `(inter ,lhs ,rhs)
      (apply inter (cons (inter lhs rhs) etc))))


; Operator symbol for blend ops.
(define (union? operator)
  (or (eq? operator union) (eq? operator 'union)))

(define (diff? operator)
  (or (eq? operator diff) (eq? operator 'diff)))

(define (inter? operator)
  (or (eq? operator inter) (eq? operator 'inter)))

(define (csg-operator? operator)
  (or (union? operator)
      (diff? operator)
      (inter? operator)))


; Blending CSG operator.
(define/contract (blend operator threshold lhs rhs . etc)
  ((csg-operator? number? csg? csg?) () #:rest (listof csg?) . ->* . (list/c (or/c 'blend-union 'blend-diff 'blend-inter) flonum? csg? csg?))
  (assert-csg lhs)
  (assert-csg rhs)
  (define op (cond
               [(union? operator) 'blend-union]
               [(diff? operator) 'blend-diff]
               [(inter? operator) 'blend-inter]))
  (define (inner lhs rhs . etc)
    (if (null? etc)
        `(,op ,(fl threshold) ,lhs ,rhs)
        (apply inner (cons (inner lhs rhs) etc))))
  (apply inner (append (list lhs rhs) etc)))


; Translation transform.
(define/contract (move x y z child)
  (number? number? number? csg? . -> . (list/c 'move flonum? flonum? flonum? csg?))
  (assert-csg child)
  `(move ,(fl x) ,(fl y) ,(fl z) ,child))


; Translation transform.
(define/contract (move-x n child)
  (number? csg? . -> . (list/c 'move flonum? flonum? flonum? csg?))
  (move n 0. 0. child))


; Translation transform.
(define/contract (move-y n child)
  (number? csg? . -> . (list/c 'move flonum? flonum? flonum? csg?))
  (move 0. n 0. child))


; Translation transform.
(define/contract (move-z n child)
  (number? csg? . -> . (list/c 'move flonum? flonum? flonum? csg?))
  (move 0. 0. n child))


; Convert from degrees to radians.
(define/contract (radians degrees)
  (number? . -> . flonum?)
  (fl (degrees->radians degrees)))


; Rotation transform.
(define/contract (rotate-x degrees child)
  (number? csg? . -> . (list/c 'quat flonum? flonum? flonum? flonum? csg?))
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
(define/contract (rotate-y degrees child)
  (number? csg? . -> . (list/c 'quat flonum? flonum? flonum? flonum? csg?))
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
(define/contract (rotate-z degrees child)
  (number? csg? . -> . (list/c 'quat flonum? flonum? flonum? flonum? csg?))
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
