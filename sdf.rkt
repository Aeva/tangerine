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
         trans)

(struct sdf-part (wrapped))

(define (scene sdf-body)
  (~a "float SceneDist(vec3 Point)\n{\n\treturn "
      ((sdf-part-wrapped sdf-body) "Point")
      ";\n}\n"))

(define (sphere diameter)
  (sdf-part
   (λ (point)
    @~a{SphereBrush(@point, @diameter)})))

(define (box width depth height)
  (sdf-part
   (λ (point)
     @~a{BoxBrush(@point, vec3(@width, @depth, @height))})))

(define (cube extent)
  (box extent extent extent))

(define (make-operator op)
  (λ (lhs rhs)
    (sdf-part
     (λ (point)
       (let ([lhs-branch ((sdf-part-wrapped lhs) point)]
             [rhs-branch ((sdf-part-wrapped rhs) point)])
         @~a{@op(@lhs-branch, @rhs-branch)})))))


(define union (make-operator "UnionOp"))
(define inter (make-operator "IntersectionOp"))
(define cut (make-operator "CutOp"))


(define (make-smooth op)
  (λ (span lhs rhs)
    (sdf-part
     (λ (point)
       (let ([lhs-branch ((sdf-part-wrapped lhs) point)]
             [rhs-branch ((sdf-part-wrapped rhs) point)])
         @~a{@op(@lhs-branch, @rhs-branch, @span)})))))


(define smooth-union (make-smooth "SmoothUnionOp"))
(define smooth-inter (make-smooth "SmoothIntersectionOp"))
(define smooth-cut (make-smooth "SmoothCutOp"))


(define (trans x y z child)
  (sdf-part
   (λ (point)
     ((sdf-part-wrapped child) @~a{(@point - vec3(@x, @y, @z))}))))

