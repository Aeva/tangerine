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
(require racket/string)


(provide eval-dist)


(define (splat args)
  (apply values args))


(define (eval-dist csgst [point "Point"])
  (let ([node (car csgst)]
        [args (cdr csgst)])
    (case node

      [(sphere)
       (let-values ([(radius) (splat args)])
         @~a{SphereBrush(@point, @radius)})]

      [(ellipsoid)
       (let-values ([(radius-x radius-y radius-z) (splat args)])
         @~a{EllipsoidBrush(@point, vec3(@radius-x, @radius-y, @radius-z))})]

      [(box)
       (let-values ([(extent-x extent-y extent-z) (splat args)])
         @~a{BoxBrush(@point, vec3(@extent-x, @extent-y, @extent-z))})]

      [(torus)
       (let-values ([(major-radius minor-radius) (splat args)])
         @~a{TorusBrush(@point, @major-radius, @minor-radius)})]

      [(cylinder)
       (let-values ([(radius extent) (splat args)])
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
       (let*-values ([(x y z child) (splat args)]
                     [(point) @~a{(@point - vec3(@x, @y, @z))}])
         (eval-dist child point))]

      [(quat)
       (let*-values ([(x y z w child) (splat args)]
                     [(x) (* -1 x)]
                     [(y) (* -1 y)]
                     [(z) (* -1 z)]
                     [(point) @~a{QuaternionTransform(@point, vec4(@x, @y, @z, @w))}])
         (eval-dist child point))]

      [else (error "Unknown CSGST node:" csgst)])))
