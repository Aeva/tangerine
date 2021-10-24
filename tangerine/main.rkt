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

(require racket/list)
(require racket/string)
(require racket/format)
(require racket/rerequire)
(require ffi/unsafe)
(require ffi/unsafe/define)
(require "csgst.rkt")
(require "bounds.rkt")
(require "coalesce.rkt")
(require "glsl.rkt")
(require "vec.rkt")

(provide compile
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
         rotate-z
         renderer-load-and-process-model)


(define-ffi-definer define-backend (ffi-lib #f) #:default-make-fail make-not-available)
(define-backend EmitShader (_fun _string/utf-8 _string/utf-8 -> _size))
(define-backend EmitSubtree (_fun _size [_size = (length params)] [params : (_list i _float)] -> _void))
(define-backend EmitSection (_fun (_list i _float) (_list i _float) (_list i _float) -> _void))
(define-backend SetLimitsCallback (_fun _float _float _float _float _float _float -> _void))
(define-backend RacketErrorCallback (_fun _string/utf-8 -> _void))


(define (glsl-vec3 vec)
  (let-values ([(x y z) (apply values vec)])
    @~a{vec3(@x, @y, @z)}))


(define (splat args)
  (apply values args))


(define (operator-parts subtree)
  (cond [(blend-operator? subtree)
         (let-values ([(op span lhs rhs) (splat subtree)])
           (values (list op span) lhs rhs))]
        [(binary-operator? subtree)
         (let-values ([(op lhs rhs) (splat subtree)])
           (values (list op) lhs rhs))]))


; This function takes the csgst output from the "coalesce" function and
; extracts all of the transforms and brush parameters and rewrites the tree to
; store them separately.  It returns two values.  The first value is the
; new subtree with the transforms removed and the brushes rewritten.  The
; second value is list of parameters.  The rewritten brushes provide the
; starting index of their parameter slice.
(define (genericize subtree [params null])
  (cond [(eq? (car subtree) 'move)
         (let*-values ([(x y z brush) (splat (cdr subtree))]
                       [(offset) (length params)]
                       [(params) (append params (list x y z))]
                       [(brush params) (genericize brush params)]
                       [(subtree) `(move ,offset ,brush)])
           (values subtree params))]

        [(eq? (car subtree) 'mat4)
         (let*-values ([(matrix brush) (splat (cdr subtree))]
                       [(offset) (length params)]
                       [(params) (append params (flatten matrix))]
                       [(brush params) (genericize brush params)]
                       [(subtree) `(mat4 ,offset ,brush)])
           (values subtree params))]

        [(brush? subtree)
         (let* ([offset (length params)]
                [params (append params (cdr subtree))]
                [subtree (list (car subtree) offset)])
           (values subtree params))]

        [(operator? subtree)
         (let*-values ([(op lhs rhs) (operator-parts subtree)]
                       [(lhs params) (genericize lhs params)]
                       [(rhs params) (genericize rhs params)]
                       [(subtree) (append op (list lhs rhs))])
           (values subtree params))]))


; This function takes the results from the second value returned by the
; "segments" function, and rewrites them such that all of the brush transforms
; and parameters are stored separately from the csg tree, using the "genericize"
; function above.
(define (drawables parts)
  (for/list ([part (in-list parts)])
    (let*-values ([(subtree) (car part)]
                  [(bounds) (cdr part)]
                  [(subtree params) (genericize subtree)])
      (list subtree params bounds))))


(define (compile csgst)
  (let*-values ([(limits parts) (segments (coalesce csgst))]
                [(parts) (drawables parts)])
    (apply SetLimitsCallback limits)
    (for/list ([part (in-list parts)]
               [subtree-index (in-range (length parts))])
      (let* ([subtree (car part)]
             [params (cadr part)]
             [bounds (caddr part)])
        (append
         (list
          subtree
          params
          (string-join
           (flatten
            (list
             "layout(std140, binding = 1)"
             "uniform SubtreeParameterBlock"
             "{"
             (for/list ([i (in-range (length params))])
               (~a "\tfloat PARAM" i ";"))
             "};\n"
             @~a{const uint SubtreeIndex = @subtree-index;}
             "float ClusterDist(vec3 Point)"
             "{"
             (~a "\treturn " (eval-dist subtree) ";")
             "}\n"))
           "\n"))
         (for/list ([bound (in-list bounds)])
           (let* ([low (car bound)]
                  [high (cdr bound)]
                  [extent (vec* 0.5 (vec- high low))]
                  [center (vec+ low extent)])
             (cons extent center))))))))


;(genericize (coalesce (move-x 10 (diff (cube 1) (sphere 1.4)))))
;(let-values ([(limits parts) (segments (coalesce (move 1 2 3 (diff (cube 1) (sphere 1.4)))))])
;            (drawables parts))
;(compile (rotate-z 45 (box 1 2 3)))


(define (renderer-load-and-process-model path-str)
  (with-handlers ([exn:fail? (λ (err) (RacketErrorCallback (exn-message err)))])
    (let ([path (string->path path-str)])
      (dynamic-rerequire path)
      (let ([clusters ((dynamic-require path 'emit-glsl))])
        (for ([cluster (in-list clusters)])
          (let* ([tree (~a (car cluster))]
                 [params (cadr cluster)]
                 [dist (caddr cluster)]
                 [aabbs (cdddr cluster)]
                 [index (EmitShader tree dist)]
                 [matrix (flatten (mat4-identity))])
            (EmitSubtree index params)
            (for ([aabb (in-list aabbs)])
              (EmitSection (car aabb) (cdr aabb) matrix)))))))
  (void))
