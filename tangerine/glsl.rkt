#lang at-exp racket/base

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

(require racket/format)
(require racket/list)
(require racket/string)
(require "vec.rkt")


(provide eval-dist)


(define (splat args)
  (apply values args))


(define (param offset)
  @~a{PARAMS[@offset]})


(define (params n offset)
  (for/list ([i (in-range n)])
     (param (+ i offset))))


(define (params-str n offset)
  (string-join (params n offset) ", "))


(define (eval-dist csgst [point "Point"])
  (let* ([node (car csgst)]
         [args (cdr csgst)])
    (case node

      [(sphere)
       (let ([radius (param (car args))])
         @~a{SphereBrush(@point, @radius)})]

      [(ellipsoid)
       (let ([radius (params-str 3 (car args))])
         @~a{EllipsoidBrush(@point, vec3(@radius))})]

      [(box)
       (let ([extent (params-str 3 (car args))])
         @~a{BoxBrush(@point, vec3(@extent))})]

      [(torus)
       (let-values ([(major-radius minor-radius) (splat (params 2 (car args)))])
         @~a{TorusBrush(@point, @major-radius, @minor-radius)})]

      [(cylinder)
       (let-values ([(radius extent) (splat (params 2 (car args)))])
         @~a{CylinderBrush(@point, @radius, @extent)})]

      [(union)
       (let-values ([(lhs rhs) (splat (map (λ (branch) (eval-dist branch point)) args))])
         @~a{UnionOp(@lhs, @rhs)})]

      [(diff)
       (let-values ([(lhs rhs) (splat (map (λ (branch) (eval-dist branch point)) args))])
         @~a{CutOp(@lhs, @rhs)})]

      [(inter)
       (let-values ([(lhs rhs) (splat (map (λ (branch) (eval-dist branch point)) args))])
         @~a{IntersectionOp(@lhs, @rhs)})]

      [(blend-union)
       (let ([threshold (car args)]
             [lhs (eval-dist (cadr args) point)]
             [rhs (eval-dist (caddr args) point)])
         @~a{SmoothUnionOp(@lhs, @rhs, @threshold)})]

      [(blend-diff)
       (let ([threshold (car args)]
             [lhs (eval-dist (cadr args) point)]
             [rhs (eval-dist (caddr args) point)])
         @~a{SmoothCutOp(@lhs, @rhs, @threshold)})]

      [(blend-inter)
       (let ([threshold (car args)]
             [lhs (eval-dist (cadr args) point)]
             [rhs (eval-dist (caddr args) point)])
         @~a{SmoothIntersectionOp(@lhs, @rhs, @threshold)})]

      [(move)
       (let* ([vec (params-str 3 (car args))]
              [child (cadr args)]
              [point @~a{(@point - vec3(@vec))}])
         (eval-dist child point))]

      [(mat4)
       (let* ([matrix (params-str 16 (car args))]
              [child (cadr args)]
              [point @~a{MatrixTransform(@point, mat4(@matrix))}])
         (eval-dist child point))]

      [(paint)
       (let* ([material (car args)]
              [child (eval-dist (cadr args) point)])
         @~a{MaterialDist(@material, @child)})]

      [else (error "Unknown CSGST node:" csgst)])))
