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

(require racket/exn)
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
(require "eval.rkt")

(provide compile
         align
         paint
         paint-over
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
(define-backend EmitShader (_fun _string/utf-8 _string/utf-8 _string/utf-8 -> _size))
(define-backend EmitSubtree (_fun _size [_size = (length params)] [params : (_list i _float)] -> _void))
(define-backend EmitSection (_fun (_list i _float) (_list i _float) (_list i _float) -> _void))
(define-backend SetLimitsCallback (_fun _float _float _float _float _float _float -> _void))
(define-backend RacketErrorCallback (_fun _string/utf-8 -> _void))


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
                [params (append params (cdr subtree))] ; Append all of the brush parameters to the end of the params list.
                [subtree (list (car subtree) offset)]) ; The subtree is now just the brush function and the offset into the params list.
           (values subtree params))]

        [(paint? subtree)
         (let*-values ([(op material subtree) (splat subtree)]
                       [(subtree params) (genericize subtree params)]
                       [(subtree) `(paint ,material ,subtree)])
           ; The material probably should go on the params block as well, but for now just pass through.
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


; Used by pretty-print below for operators.
(define (pretty-print-op head operands)
  (define tmp (~a "⎛" head))
  (let* ([treext (string-join operands "\n")]
         [lines (string-split treext "\n")])
    (for ([i (in-naturals 1)]
          [line lines])
      (let* ([last? (eq? i (length lines))]
             [sep (if last? "⎝" "⎜")])
        (set! tmp (string-append tmp "\n" sep "  " line)))))
  tmp)


; Pretty print the CSG tree for debugging.
(define (pretty-print csgst)
  (let ([node (car csgst)])
    (cond [(or
            (eq? node 'union)
            (eq? node 'inter)
            (eq? node 'diff))
           (pretty-print-op node (map pretty-print (cdr csgst)))]
          [(or
            (eq? node 'blend-union)
            (eq? node 'blend-inter)
            (eq? node 'blend-diff))
           (let ([threshold (cadr csgst)]
                 [subtrees (cddr csgst)])
             (pretty-print-op (~a node " " threshold)
                              (map pretty-print subtrees)))]
          [else (~a csgst)])))


; This function compiles a CSG tree into a set of partial GLSL sources and
; related parameter data, which is to be later used by tangerine to compose
; shaders and parameter buffers.  Model files should define a function called
; "emit-glsl", which calls this function and returns the results without
; modification.  This allows for checking the validity of the model offline
; without running tangerine.
(define (compile csgst)
  (let*-values
      (; Perform transform folding
       [(coalesced-tree) (coalesce csgst)]

       ; Find the model boundaries, and bounded subtrees
       [(limits parts) (segments coalesced-tree)]

       ; Create an evaluator for the model.
       [(evaluator) (sdf-build coalesced-tree)]

       ; Convert the bounded subtrees into bounded subtrees with extracted
       ; parameters.  Each "part" is in the form (list subtree params bounds).
       [(parts) (drawables parts)])

    ; Generate GLSL functions and instancing data for each shader to compile.
    ; This will be processed further by "renderer-load-and-process-model".
    (values
     limits
     evaluator
     (for/list ([part (in-list parts)]
                [subtree-index (in-range (length parts))])
       (let* ([subtree (car part)]
              [params (cadr part)]
              [bounds (caddr part)]
              [glsl (generate-glsl subtree subtree-index)])
         (append
          (list
           subtree
           params
           glsl)
          (for/list ([bound (in-list bounds)])
            (let* ([low (car bound)]
                   [high (cdr bound)]
                   [extent (vec* 0.5 (vec- high low))]
                   [center (vec+ low extent)])
              (cons extent center)))))))))


; This is called by the tangerine application to invoke the shape compiler for
; a given model source, and pass the results into the renderer.  The generated
; GLSL will be combined with additional GLSL sources there and passed off to
; OpenGL's GLSL compiler.  If all goes well, the resulting shader object will
; be used to render the model.
(define (renderer-load-and-process-model path-str)
  (with-handlers ([exn:fail? (λ (err) (RacketErrorCallback (exn->string err)))])
    (let ([path (string->path path-str)])
      (dynamic-rerequire path)
      (let*-values ([(compiler) (dynamic-require path 'emit-glsl)]
                    [(limits evaluator clusters) (compiler)])

        ; Set the model bounds used by the renderer.
        (apply SetLimitsCallback limits)

        ; Set the model evaluator to be used by the STL exporter.
        (when (sdf-handle-is-valid? evaluator)
          (SetTreeEvaluator (cdr evaluator)))

        ; Emit shaders for the renderer to finish compiling, and instance
        ; data for the renderer to draw.
        (for ([cluster (in-list clusters)])
          (let* ([tree (car cluster)]
                 [params (cadr cluster)]
                 [dist (caddr cluster)]
                 [aabbs (cdddr cluster)]
                 [index (EmitShader (~a tree) (pretty-print tree) dist)]
                 [matrix (flatten (mat4-identity))])
            (EmitSubtree index params)
            (for ([aabb (in-list aabbs)])
              (EmitSection (car aabb) (cdr aabb) matrix)))))))
  (collect-garbage)
  (void))
