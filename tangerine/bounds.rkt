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


(provide segments)


(define (splat args)
  (apply values args))


; Is it an AABB?
(define (aabb? thing)
  (and
   (list? thing)
   (vec3? (car thing))
   (vec3? (cadr thing))
   (csg? (caddr thing))))


; Is it a list of AABBs?
(define (aabb-list? thing)
  (andmap aabb? thing))


; Test if a point is within an AABB's bounds, inclusive of the edges.
(define (within? point aabb)
  (and
   (vec= point (vec-max point (car aabb)))
   (vec= point (vec-min point (cadr aabb)))))


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

  (if (null? bounds)
      null
      (let* ([subtrees (extract-subtrees bounds)]
             [groups (for/list ([subtree (in-list subtrees)])
                       (for/list ([aabb (in-list bounds)]
                                  #:when (equal? (caddr aabb) subtree))
                         aabb))]
             [merged (map merge-group groups)])
        (if (eq? 1 (length merged))
            (car merged)
            (merge-group merged (apply combiner subtrees))))))


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


; Returns a list of lines for a given AABB's edges parallel to the given axis.
(define (aabb-edge aabb axis)
  (let* ([low (car aabb)]
         [high (cadr aabb)]
         [lx (car low)]
         [ly (cadr low)]
         [lz (caddr low)]
         [hx (car high)]
         [hy (cadr high)]
         [hz (caddr high)])
    (cond
      [(eq? axis 0)
       (list ; Edge lines parallel to the x-axis.
        (vec3 0. ly lz)
        (vec3 0. ly hz)
        (vec3 0. hy lz)
        (vec3 0. hy hz))]
      [(eq? axis 1)
       (list ; Edge lines parallel to the y-axis.
        (vec3 lx 0. lz)
        (vec3 lx 0. hz)
        (vec3 hx 0. lz)
        (vec3 hx 0. hz))]
      [(eq? axis 2)
       (list ; Edge lines parallel to the z-axis.
        (vec3 lx ly 0.)
        (vec3 lx hy 0.)
        (vec3 hx ly 0.)
        (vec3 hx hy 0.))])))


; Return the minimum and and maximum coordinates of an AABB for a given axis.
(define (limits aabb axis)
  (list
   (list-ref (car aabb) axis)
   (list-ref (cadr aabb) axis)))


; Returns a list of intersection points between two AABBs.
(define (aabb-aabb lhs rhs)

  (define (axis-mask axis)
    (cond [(eq? axis 0) '(1. 0. 0.)]
          [(eq? axis 1) '(0. 1. 0.)]
          [(eq? axis 2) '(0. 0. 1.)]))

  (define (inner caster occluder)
    (append*
     (for/list ([axis (in-range 3)])
       (let ([mask (axis-mask axis)]
             [edges (aabb-edge caster axis)])
         (append*
          (for/list ([limit (in-list (limits occluder axis))])
            (let ([plane (vec* mask limit)])
              (filter (λ (pt)
                        (and (within? pt caster)
                             (within? pt occluder)))
                      (for/list ([line (in-list edges)])
                        (vec+ plane line))))))))))
  (remove-duplicates
   (append
    (inner lhs rhs)
    (inner rhs lhs))))


; Split an AABB by the corners of the clipping AABB, and return the list
; of new AABBs which are not occluded by the clipping AABB.
(define (aabb-clip aabb clip)
  (define (shatter bounds points)
    (if (null? points)
        bounds
        (let* ([pivot (car points)]
               [next (cdr points)]
               [bounds
                (append*
                 (for/list ([aabb (in-list bounds)])
                   (aabb-split aabb pivot)))])
          (shatter bounds next))))

  (define (keep? aabb)
    (not (occludes? clip aabb)))

  (let* ([pivots (remove-duplicates
                  (append
                   (corners clip)
                   (aabb-aabb aabb clip)))]
         [shards (shatter (list aabb) pivots)]
         [result (filter keep? shards)])
    (if (null? result)
        (list
         (list (vec3 0.0)
               (vec3 0.0)
               (caddr aabb)))
        result)))


; Adjust the bounds of an AABB.
(define (pad-aabb amount aabb)
  (list
   (vec- (car aabb) amount)
   (vec+ (cadr aabb) amount)
   (caddr aabb)))


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
         (if (equal? lhs rhs)
             (tree-aabb lhs)
             (append (tree-aabb lhs)
                     (tree-aabb rhs))))]

      [(blend-union)
       (let-values ([(threshold lhs rhs) (splat args)])
         (let* ([liminal (* threshold threshold 0.25 (rcp threshold))]
                [lhs-aabbs (tree-aabb lhs)]
                [rhs-aabbs (tree-aabb rhs)]
                [lhs-merged (aabb-union lhs-aabbs)]
                [rhs-merged (aabb-union rhs-aabbs)]
                [blend (rewrite-subtree
                        (aabb-inter
                         (pad-aabb threshold lhs-merged)
                         (pad-aabb threshold rhs-merged))
                        (blend union threshold lhs rhs))])
           (cons blend
                 (append*
                  (for/list ([aabb (in-list (append lhs-aabbs rhs-aabbs))])
                    (aabb-clip aabb blend))))))]

      [(diff)
       (let-values ([(lhs rhs) (splat args)])
         (let* ([lhs-aabbs (tree-aabb lhs)]
                [rhs-aabbs (tree-aabb rhs)]
                [lhs-merged (aabb-union lhs-aabbs)]
                [rhs-merged (aabb-union rhs-aabbs)]
                [diff-region (rewrite-subtree
                              (aabb-inter lhs-merged rhs-merged)
                              (diff lhs
                                    rhs))])
           (cons diff-region
                 (append*
                  (for/list ([aabb (in-list lhs-aabbs)])
                    (aabb-clip aabb diff-region))))))]

      [(blend-diff)
       (let-values ([(threshold lhs rhs) (splat args)])
         (let* ([liminal (* threshold threshold 0.25 (rcp threshold))]
                [lhs-aabbs (tree-aabb lhs)]
                [rhs-aabbs (tree-aabb rhs)]
                [lhs-merged (aabb-union lhs-aabbs)]
                [rhs-merged (aabb-union rhs-aabbs)]
                [diff-region (rewrite-subtree
                              (aabb-inter
                               (pad-aabb liminal lhs-merged)
                               (pad-aabb liminal rhs-merged))
                              (blend diff threshold lhs rhs))])
           (cons diff-region
                 (append*
                  (for/list ([aabb (in-list lhs-aabbs)])
                    (aabb-clip aabb diff-region))))))]

      [(inter)
       (let-values ([(lhs rhs) (splat args)])
         (if (equal? lhs rhs)
             (tree-aabb lhs)
             (let* ([lhs-aabbs (tree-aabb lhs)]
                    [rhs-aabbs (tree-aabb rhs)]
                    [lhs-merged (aabb-union lhs-aabbs)]
                    [rhs-merged (aabb-union rhs-aabbs)]
                    [inter-region (rewrite-subtree
                                   (aabb-inter lhs-merged rhs-merged)
                                   (inter lhs
                                          rhs))])
               (list inter-region))))]

      [(blend-inter)
       (let-values ([(threshold lhs rhs) (splat args)])
         (if (equal? lhs rhs)
             (tree-aabb lhs)
             (let* ([lhs-aabbs (tree-aabb lhs)]
                    [rhs-aabbs (tree-aabb rhs)]
                    [lhs-merged (aabb-union lhs-aabbs)]
                    [rhs-merged (aabb-union rhs-aabbs)]
                    [inter-region (rewrite-subtree
                                   (aabb-inter lhs-merged rhs-merged)
                                   (blend inter threshold lhs rhs))])
               (list inter-region))))]

      [(move)
       (let-values ([(x y z child) (splat args)])
         (let ([delta (vec3 x y z)]
               [bounds (tree-aabb child)])
           (for/list ([aabb bounds])
             (let ([low (vec+ (car aabb) delta)]
                   [high (vec+ (cadr aabb) delta)]
                   [subtree (caddr aabb)])
               (list low high (move x y z subtree))))))]

      [(quat)
       (let-values ([(x y z w child) (splat args)])
         (let* ([quat (vec4 x y z w)]
                [bounds (tree-aabb child)])
           (for/list ([aabb bounds])
             (let* ([points (map (λ (pt) (quat-rotate pt quat))
                                 (corners aabb))]
                    [low (apply vec-min points)]
                    [high (apply vec-max points)]
                    [subtree (caddr aabb)])
               (list low high `(quat ,x ,y ,z ,w ,subtree))))))]

      [(mat4)
       (let*-values ([(matrix child) (splat args)]
                     [(bounds) (tree-aabb child)])
         (for/list ([aabb bounds])
           (let* ([points (map (λ (pt) (apply-matrix pt matrix))
                               (corners aabb))]
                  [low (apply vec-min points)]
                  [high (apply vec-max points)]
                  [subtree (caddr aabb)])
             (list low high `(mat4 ,matrix ,subtree)))))]

      [else (error "Unknown CSGST node:" node)])))


(define (segments csg-tree)
  (assert-csg csg-tree)
  (let* ([bounds
          (remove-duplicates
           (filter aabb-valid?
                   (tree-aabb csg-tree)))]
         [subtrees (extract-subtrees bounds)])
    (for/list ([subtree (in-list subtrees)])
      (cons subtree
            (remove-duplicates
             (for/list ([aabb (in-list bounds)]
                        #:when (equal? (caddr aabb) subtree))
               (cons (car aabb)
                     (cadr aabb))))))))


; Tests
;(tree-aabb (cube 2))
;(tree-aabb (move-x 1 (cube 2)))
;(tree-aabb (rotate-z 45 (cube 2)))
;(tree-aabb (union (cube 2) (cube 2)))
;(tree-aabb (union (cube 1) (cube 2)))
;(tree-aabb (union (cube 2) (move 1 1 1 (cube 2))))
;(tree-aabb (diff (cube 2) (sphere 2.2)))
;(tree-aabb (diff (sphere 2.2) (cube 2)))
;(tree-aabb (diff (cube 2) (cube 2)))
;(tree-aabb (diff (cube 2) (union (move-x 2 (cube 2))
;                                 (move-x -2 (cube 2))
;                                 (cube 1))))
;(tree-aabb (inter (move-x -1 (cube 2))
;                  (move-x 1 (cube 2))))
;(tree-aabb (inter (cube 2) (cube 2)))
;(tree-aabb (blend union 1 (sphere 2) (move-x 1 (sphere 2))))
;(filter aabb-valid?
;        (tree-aabb (blend union 1
;                          (sphere 3)
;                          (move-x 1.5 (sphere 2))
;                          (move-x -1.5 (sphere 2)))))
