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
(require racket/math)

(provide scene
         sphere
         box
         cube
         torus
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
         quat
         quat-id
         quat-rot-x
         quat-rot-y
         quat-rot-z
         rotate)


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


(define (torus major-diameter minor-diameter)
  (let* ([minor-radius (/ minor-diameter 2.)]
         [major-radius (- (/ major-diameter 2.) minor-radius)])
  (sdf-part
   (λ (point)
     @~a{TorusBrush(@point, @major-radius, @minor-radius)})
   (λ ()
     @~a{TorusBrushBounds(@major-radius, @minor-radius)}))))


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


(struct quat (x y z w))


(define (quat->values qt)
  (values
   (quat-x qt)
   (quat-y qt)
   (quat-z qt)
   (quat-w qt)))


(define (quat-id)
  (quat 0. 0. 0. 1.))


(define (normalize-quat qt)
  (let-values ([(x y z w) (quat->values qt)])
    (let ([dot-quat (+ (* x x)
                       (* y y)
                       (* z z)
                       (* w w))])
      (quat (/ x dot-quat)
            (/ y dot-quat)
            (/ z dot-quat)
            (/ w dot-quat)))))


(define (inverse-quat qt)
  (let-values([(x y z w) (quat->values (normalize-quat qt))])
    (quat (* -1. x)
          (* -1. y)
          (* -1. z)
          w)))


(define (quat-rot-x deg [qt (quat-id)])
  (let-values([(x y z w) (quat->values qt)])
    (let* ([rad (/ (degrees->radians deg) 2.0)]
           [s (sin rad)]
           [c (cos rad)])
      (quat
       (+ (* x c) (* w s))
       (+ (* y c) (* z s))
       (- (* z c) (* y s))
       (- (* w c) (* x s))))))


(define (quat-rot-y deg [qt (quat-id)])
  (let-values([(x y z w) (quat->values qt)])
    (let* ([rad (/ (degrees->radians deg) 2.0)]
           [s (sin rad)]
           [c (cos rad)])
      (quat
       (- (* x c) (* z s))
       (+ (* y c) (* w s))
       (+ (* z c) (* x s))
       (- (* w c) (* y s))))))


(define (quat-rot-z deg [qt (quat-id)])
  (let-values([(x y z w) (quat->values qt)])
    (let* ([rad (/ (degrees->radians deg) 2.0)]
           [s (sin rad)]
           [c (cos rad)])
      (quat
       (+ (* x c) (* y s))
       (- (* y c) (* x s))
       (+ (* z c) (* w s))
       (- (* w c) (* z s))))))


(define (rotate qt child)
  (sdf-part
   (let-values([(x y z w) (quat->values (inverse-quat qt))])
     (λ (point)
       ((sdf-part-dist child) @~a{QuaternionTransform(@point, vec4(@x, @y, @z, @w))})))
   (let-values([(x y z w) (quat->values (normalize-quat qt))])
     (λ ()
       (let ([aabb ((sdf-part-aabb child))])
         @~a{QuaternionTransformAABB(@aabb, vec4(@x, @y, @z, @w))})))))
