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


(require racket/list)
(require "csgst.rkt")
(require "vec.rkt")


(define (splat args)
  (apply values args))


; Test if a point is within an AABB's bounds, inclusive of the edges.
(define (within? point aabb)
  (and
   (equal? point (vec-max point (car aabb)))
   (equal? point (vec-min point (cadr aabb)))))


; Emit the low point, high point, and subtree of the AABB as values.
(define (aabb->values aabb)
  (values (car aabb)
          (cadr aabb)
          (caddr aabb)))


; Test if the AABB has volume.
(define (aabb-valid? aabb)
  (let-values ([(low high child) (aabb->values aabb)])
    (and
     (< (car low) (car high))
     (< (cadr low) (cadr high))
     (< (caddr low) (caddr high)))))


; Returns the AABB's corner points in a list.
(define (corners aabb)
  (let* ([a (car aabb)]
         [b (cadr aabb)])
    (list
     a b
     (vec3 (swiz b 0) (swiz a 1 2))
     (vec3 (swiz a 0) (swiz b 1) (swiz a 2))
     (vec3 (swiz a 0 1) (swiz b 2))
     (vec3 (swiz a 0) (swiz b 1 2))
     (vec3 (swiz b 0) (swiz a 1) (swiz b 2))
     (vec3 (swiz b 0 1) (swiz a 2)))))


; Returns the AABB for the intersection of two AABBs.
(define (aabb-inter lhs rhs [combiner inter])
  (let-values ([(lhs-min lhs-max lhs-tree) (aabb->values lhs)]
               [(rhs-min rhs-max rhs-tree) (aabb->values rhs)])
    (list
     (vec-max lhs-min rhs-min)
     (vec-min lhs-max rhs-max)
     (combiner lhs-tree rhs-tree))))


; Extract a list of subtrees from a list of AABBs.
(define (extract-subtrees bounds)
  (remove-duplicates
   (for/list ([aabb (in-list bounds)])
     (caddr aabb))))


; Rewrite the subtree in an AABB.
(define (rewrite-subtree aabb new-subtree)
  (list (car aabb)
        (cadr aabb)
        new-subtree))


; Merge a list of AABBs into a single AABB.  This will try to consolidate
; AABBs by subtree before adding any new combining nodes.
(define (aabb-union bounds [combiner union])
  (define (merge-group group [subtree #f])
    (if (> (length group) 1)
        (list
         (apply vec-min (for/list ([aabb (in-list group)]) (car aabb)))
         (apply vec-max (for/list ([aabb (in-list group)]) (cadr aabb)))
         (or subtree (caddar group)))
        (car group)))

  (let* ([subtrees (extract-subtrees bounds)]
         [groups (for/list ([subtree (in-list subtrees)])
                   (for/list ([aabb (in-list bounds)]
                              #:when (equal? (caddr aabb) subtree))
                     aabb))]
         [merged (map merge-group groups)])
    (if (> (length merged) 1)
        (car (merge-group merged (apply combiner subtrees)))
        (car merged))))


; Split an AABB upon the given pivot point.  If the point is outside the AABB
; or upon one of the corners, a list containing the original AABB is returned.
(define (aabb-split aabb pivot)
  (if (within? pivot aabb)
      (let ([points (corners aabb)]
            [tree (caddr aabb)])
        (filter aabb-valid?
                (for/list ([point (in-list points)])
                  (let ([low (vec-min point pivot)]
                        [high (vec-max point pivot)])
                    (list low high tree)))))
      (list aabb)))


; Determine if the left hand AABB is a superset of the right hand AABB.
(define (occludes? lhs rhs)
  (let-values ([(lhs-min lhs-max lhs-child) (aabb->values lhs)]
               [(rhs-min rhs-max rhs-child) (aabb->values rhs)])
    (and (equal? lhs-min (vec-min lhs-min rhs-min))
         (equal? lhs-max (vec-max lhs-max rhs-max)))))


; Split an AABB by the corners of the clipping AABB, and return the list
; of new AABBs which are not occluded by the clipping AABB.
(define (aabb-clip aabb clip)
  (define (inner bounds points)
    (if (null? points)
        bounds
        (let* ([pivot (car points)]
               [next (cdr points)]
               [bounds
                (append*
                 (for/list ([aabb (in-list bounds)])
                   (aabb-split aabb pivot)))])
          (inner bounds next))))
  (filter-not
   (λ (aabb)
     (occludes? clip aabb))
   (inner (list aabb) (corners clip))))


; Convert a CSG syntax tree into list of AABBs of subtrees.
(define (tree-aabb root)
  (let ([node (car root)]
        [args (cdr root)])
    (case node

      [(sphere)
       (let* ([high (apply vec3 args)]
              [low (vec* -1. high)])
         (list (list low high root)))]

      [(ellipsoid)
       (let* ([high (apply vec3 args)]
              [low (vec* -1. high)])
         (list (list low high root)))]

      [(box)
       (let* ([high (apply vec3 args)]
              [low (vec* -1. high)])
         (list (list low high root)))]

      [(torus)
       (let-values ([(major-radius minor-radius) (splat args)])
         (let* ([radius (+ major-radius minor-radius)]
                [high (vec3 radius radius minor-radius)]
                [low (vec* -1 high)])
           (list (list low high root))))]

      [(cylinder)
       (let-values ([(radius extent) (splat args)])
         (let* ([high (vec3 radius radius extent)]
                [low (vec* -1 high)])
           (list (list low high root))))]

      [(union)
       (let-values ([(lhs rhs) (splat args)])
         (let* ([lhs (tree-aabb lhs)]
                [rhs (tree-aabb rhs)]
                [lhs-merged (aabb-union lhs)]
                [rhs-merged (aabb-union rhs)]
                [interior (rewrite-subtree
                           (aabb-inter lhs-merged rhs-merged)
                           (apply union
                                  (append
                                   (extract-subtrees lhs)
                                   (extract-subtrees rhs))))])
           (cons interior
                 (append*
                  (for/list ([aabb (in-list (append lhs rhs))])
                    (aabb-clip aabb interior))))))]

      [(move)
       (let-values ([(x y z child) (splat args)])
         (let ([delta (vec3 x y z)]
               [bounds (tree-aabb child)])
           (for/list ([aabb bounds])
             (let ([low (vec+ (car aabb) delta)]
                   [high (vec+ (cadr aabb) delta)])
               (list low high root)))))]

      [(quat)
       (let-values ([(x y z w child) (splat args)])
         (let* ([quat (vec4 x y z w)]
                [bounds (tree-aabb child)])
           (for/list ([aabb bounds])
             (let* ([points (map (λ (pt) (quat-rotate pt quat))
                                 (corners aabb))]
                    [low (apply vec-min points)]
                    [high (apply vec-max points)])
               (list low high root)))))])))
