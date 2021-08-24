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

(provide scene
         sphere
         box
         cube
         union
         inter
         cut
         smooth-union
         smooth-inter
         smooth-cut
         trans
         trans-x
         trans-y
         trans-z
         quat)


(struct sdf-part (dist aabb))


(define (scene-dist sdf-body)
  (~a "float SceneDist(vec3 Point)\n{\n\treturn "
      ((sdf-part-dist sdf-body) "Point")
      ";\n}\n"))


(define (scene-bounds sdf-body)
  (~a "AABB SceneBounds()\n{\n\treturn "
      ((sdf-part-aabb sdf-body))
      ";\n}\n"))


(define (scene sdf-body)
  (~a (scene-dist sdf-body)
      "\n"
      (scene-bounds sdf-body)
      "\n"))


(define (sphere diameter)
  (sdf-part
   (λ (point)
    @~a{SphereBrush(@point, @diameter * 0.5)})
   (λ ()
    @~a{SphereBrushBounds(@diameter * 0.5)})))


(define (box width depth height)
  (sdf-part
   (λ (point)
     @~a{BoxBrush(@point, vec3(@width, @depth, @height) * 0.5)})
   (λ ()
     @~a{BoxBrushBounds(vec3(@width, @depth, @height) * 0.5)})))


(define (cube extent)
  (box extent extent extent))


(define (make-operator op)
  (let ([bounds (string-append op "Bounds")])
    (λ (lhs rhs)
      (sdf-part
       (λ (point)
         (let ([lhs-branch ((sdf-part-dist lhs) point)]
               [rhs-branch ((sdf-part-dist rhs) point)])
           @~a{@op(@lhs-branch, @rhs-branch)}))
       (λ ()
         (let ([lhs-branch ((sdf-part-aabb lhs))]
               [rhs-branch ((sdf-part-aabb rhs))])
           @~a{@bounds(@lhs-branch, @rhs-branch)}))))))


(define union (make-operator "UnionOp"))
(define inter (make-operator "IntersectionOp"))
(define cut (make-operator "CutOp"))


(define (make-smooth op)
  (let ([bounds (string-append op "Bounds")])
    (λ (span lhs rhs)
      (sdf-part
       (λ (point)
         (let ([lhs-branch ((sdf-part-dist lhs) point)]
               [rhs-branch ((sdf-part-dist rhs) point)])
           @~a{@op(@lhs-branch, @rhs-branch, @span)}))
       (λ ()
         (let ([lhs-branch ((sdf-part-aabb lhs))]
               [rhs-branch ((sdf-part-aabb rhs))])
           @~a{@bounds(@lhs-branch, @rhs-branch, @span)}))))))


(define smooth-union (make-smooth "SmoothUnionOp"))
(define smooth-inter (make-smooth "SmoothIntersectionOp"))
(define smooth-cut (make-smooth "SmoothCutOp"))


(define (trans x y z child)
  (sdf-part
   (λ (point)
     ((sdf-part-dist child) @~a{(@point - vec3(@x, @y, @z))}))
   (λ ()
     (let ([aabb ((sdf-part-aabb child))])
       @~a{TranslateAABB(@aabb, vec3(@x, @y, @z))}))))


(define (trans-x x child)
  (trans x 0. 0. child))


(define (trans-y y child)
  (trans 0. y 0. child))


(define (trans-z z child)
  (trans 0. 0. z child))


(define (normalize-quat x y z w)
  (let ([dot-quat (+ (* x x)
                (* y y)
                (* z z)
                (* w w))])
    (values (/ x dot-quat)
            (/ y dot-quat)
            (/ z dot-quat)
            (/ w dot-quat))))


(define (inverse-quat x y z w)
  (let-values([(x y z w) (normalize-quat x y z w)])
    (values (* -1. x)
            (* -1. y)
            (* -1. z)
            w)))


(define (quat x y z w child)
    (sdf-part
     (let-values([(x y z w) (normalize-quat x y z w)])
       (λ (point)
         ((sdf-part-dist child) @~a{QuaternionTransform(@point, vec4(@x, @y, @z, @w))})))
     (let-values([(x y z w) (inverse-quat x y z w)])
       (λ ()
         (let ([aabb ((sdf-part-aabb child))])
           @~a{QuaternionTransformAABB(@aabb, vec4(@x, @y, @z, @w))})))))
