#lang racket/base

; Copyright 2022 Aeva Palecek
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
(require "csgst.rkt")
(require "glsl.rkt")
(require "vec.rkt")

(provide assemble
         drawables)


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


; Translates compiled csg expressions into glsl.  The "limits" and "evaluator"
; args are passed through as return values.  This formats all of the compiled
; data in the structure expected by "renderer-load-and-process-model".
(define (assemble limits evaluator bounded-trees)

  ; Convert the bounded subtrees into bounded subtrees with extracted
  ; parameters.  Each "part" is in the form (list subtree params bounds).
  (let ([parts (drawables bounded-trees)])

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
