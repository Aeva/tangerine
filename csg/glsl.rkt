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


(provide generate-glsl)


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


; Note: the culling? arg is a special case for variant testing in the cluster culling
; pass.  This is intentionally only propogated by transforms, and only is important
; for the the union op variants.
(define (eval-bounds csgst [culling? #f])
  (let ([node (car csgst)]
        [args (cdr csgst)])
    (case node

      [(sphere)
       (let-values ([(radius) (splat args)])
         @~a{SphereBrushBounds(@radius)})]

      [(ellipsoid)
       (let-values ([(radius-x radius-y radius-z) (splat args)])
         @~a{EllipsoidBrushBounds(vec3(@radius-x, @radius-y, @radius-z))})]

      [(box)
       (let-values ([(extent-x extent-y extent-z) (splat args)])
         @~a{BoxBrushBounds(vec3(@extent-x, @extent-y, @extent-z))})]

      [(torus)
       (let-values ([(major-radius minor-radius) (splat args)])
         @~a{TorusBrushBounds(@major-radius, @minor-radius)})]

      [(cylinder)
       (let-values ([(radius extent) (splat args)])
         @~a{CylinderBrushBounds(@radius, @extent)})]

      [(union)
       (let-values ([(lhs rhs) (splat (map (λ (branch) (eval-bounds branch)) args))])
         (if culling?
             @~a{IntersectionOpBounds(@lhs, @rhs)}
             @~a{UnionOpBounds(@lhs, @rhs)}))]

      [(diff)
       (let-values ([(lhs rhs) (splat (map (λ (branch) (eval-bounds branch)) args))])
         @~a{CutOpBounds(@lhs, @rhs)})]

      [(inter)
       (let-values ([(lhs rhs) (splat (map (λ (branch) (eval-bounds branch)) args))])
         @~a{IntersectionOpBounds(@lhs, @rhs)})]

      [(blend-union)
       (let ([threshold (car args)]
             [lhs (eval-bounds (cadr args))]
             [rhs (eval-bounds (caddr args))])
         (if culling?
             @~a{ThresholdBounds(@lhs, @rhs, @threshold)}
             @~a{SmoothUnionOpBounds(@lhs, @rhs, @threshold)}))]

      [(blend-diff)
       (let ([threshold (car args)]
             [lhs (eval-bounds (cadr args))]
             [rhs (eval-bounds (caddr args))])
         @~a{SmoothCutOpBounds(@lhs, @rhs, @threshold)})]

      [(blend-inter)
       (let ([threshold (car args)]
             [lhs (eval-bounds (cadr args))]
             [rhs (eval-bounds (caddr args))])
         @~a{SmoothIntersectionOpBounds(@lhs, @rhs, @threshold)})]

      [(move)
       (let*-values ([(x y z child) (splat args)]
                     [(child) (eval-bounds child culling?)])
         @~a{TranslateAABB(@child, vec3(@x, @y, @z))})]

      [(quat)
       (let*-values ([(x y z w child) (splat args)]
                     [(child) (eval-bounds child culling?)])
         @~a{QuaternionTransformAABB(@child, vec4(@x, @y, @z, @w))})]

      [else (error "Unknown CSGST node:" csgst)])))


; Used by selectral to re-apply a stack of transforms to the base of a subtree.
(define (stitch root transforms)
  (if (null? transforms)
      root
      (stitch
       (append (car transforms) (list root))
       (cdr transforms))))


; Used by selectral to remove a transform from the tree to re-apply later.
(define (peel root)
  (let ([node (car root)]
        [args (cdr root)])
    (case node
      [(move)
       (let-values ([(x y z child) (splat args)])
         (values `(move ,x ,y ,z) child))]
      [(quat)
       (let-values ([(x y z w child) (splat args)])
         (values `(quat ,x ,y ,z ,w) child))])))


; This function takes a CSG tree and returns a list of all possible subtrees,
; preserving inherited transforms.
(define (selectral root [routes '()] [transforms '()])
  (if (null? root)
      routes
      (let ([node (car root)]
            [args (cdr root)])
        (case node

          ; Push transforms onto the stack and recur on the transform's child.
          [(move quat)
           (let-values ([(transform new-root) (peel root)])
             (selectral new-root routes (cons transform transforms)))]

          ; Unions produce a subtree for the intersection of both branches, and
          ; one for each branch.
          [(union)
           (let-values ([(left right) (splat args)])
             (let* ([traverse-right (selectral right routes transforms)]
                    [traverse-left (selectral left traverse-right transforms)])
               (cons (stitch root transforms) traverse-left)))]
          [(union-blend)
           (let-values ([(threshold left right) (splat args)])
             (let* ([traverse-right (selectral right routes transforms)]
                    [traverse-left (selectral left traverse-right transforms)])
               (cons (stitch root transforms) traverse-left)))]

          ; Diffs only produce a subtree for the intersection of both branches,
          ; and one for the left branch.
          [(diff blend-diff)
           (let ([left (cadr root)])
             (cons (stitch root transforms)
                   (selectral left routes transforms)))]

          ; Everything else terminates the current branch.
          [else (cons (stitch root transforms) routes)]))))


(define (scene-select subtrees)
  (string-append
   "int SceneSelect(mat4 WorldToClip, vec4 Tile)\n"
   "{\n"
   (string-append*
    (for/list ([index (in-naturals 0)]
               [subtree subtrees])
      (let ([bounds (eval-bounds subtree #t)])
        (~a "\tif (ClipTest(WorldToClip, Tile, " bounds "))\n"
            "\t{\n"
            "\t\t return " index ";\n"
            "\t}\n"))))
   "\treturn -1;\n"
   "}\n"))


(define (subtree-aabb subtrees)
  (string-append
   "AABB SubtreeBounds(uint Variant)\n"
   "{\n"
   "\tswitch(Variant)\n"
   "\t{\n"
   (string-append*
    (for/list ([index (in-naturals 0)]
               [subtree subtrees])
      (let ([bounds (eval-bounds subtree #f)])
        (~a "\tcase " index ":\n"
            "\t\treturn " bounds ";\n"))))
   "\tdefault:\n"
   "\t\treturn AABB(vec3(0.0), vec3(0.0));\n"
   "\t}\n"
   "}\n"))


(define (subtree-dist subtrees)
  (string-append
   (string-append*
    (for/list ([index (in-naturals 0)]
               [subtree subtrees])
      (let ([query (eval-dist subtree)])
        (~a
         "float SubtreeDist" index "(vec3 Point)\n"
         "{\n"
         "\treturn " query ";\n"
         "}\n\n"))))
   
   "float SubtreeDist(uint Variant, vec3 Point)\n"
   "{\n"
   "\tswitch(Variant)\n"
   "\t{\n"
   (string-append*
    (for/list ([index (in-naturals 0)]
               [subtree subtrees])
      (~a "\tcase " index ":\n"
          "\t\treturn SubtreeDist" index "(Point);\n")))
   "\tdefault:\n"
   "\t\treturn 0.0 / 0.0;\n"
   "\t}\n"
   "}\n"))


(define (scene-dist csgst)
  (~a "float SceneDist(vec3 Point)\n{\n\treturn "
      (eval-dist csgst)
      ";\n}\n"))


(define (scene-aabb csgst)
  (~a "AABB SceneBounds()\n{\n\treturn "
      (eval-bounds csgst)
      ";\n}\n"))


(define (generate-glsl csgst)
  (let ([subtrees (selectral csgst)])
    (~a (scene-select subtrees)
        "\n"
        (subtree-aabb subtrees)
        "\n"
        (subtree-dist subtrees))))