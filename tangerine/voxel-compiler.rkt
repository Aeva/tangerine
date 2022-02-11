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

(require racket/generator)
(require racket/list)
(require "bounds.rkt")
(require "coalesce.rkt")
(require "compiler-common.rkt")
(require "eval.rkt")
(require "vec.rkt")
(require "profiling.rkt")

(provide voxel-compiler)


(define (splat args)
  (apply values args))


(define (find-grid limits voxel-size)
  (let* ([low (car limits)]
         [high (cadr limits)]
         [actual-extent (vec- high low)]
         [voxel-count (vec/↑ actual-extent (vec3 voxel-size))]
         [aligned-extent (vec* voxel-size voxel-count)]
         [padding (vec* (vec- aligned-extent actual-extent) (vec3 .5))]
         [aligned-low (vec- low padding)]
         [aligned-high (vec+ high padding)])
    (values aligned-low aligned-high voxel-count)))


(define (vox-and-clip limits evaluator voxel-size)
  (let*-values
      ([(model-low) (car limits)]
       [(model-high) (cadr limits)]
       [(grid-low grid-high voxel-count) (find-grid limits voxel-size)]
       [(half-voxel) (vec* voxel-size (vec3 .5))]
       [(radius) (vec-len half-voxel)]
       [(start-x start-y start-z) (splat (vec+ grid-low half-voxel))]
       [(stop-x stop-y stop-z) (splat grid-high)]
       [(step-x step-y step-z) (splat (vec3 voxel-size))]
       [(voxels)
        (for/list ([clip (in-generator
                          (for* ([vz (in-range start-z stop-z step-z)]
                                 [vy (in-range start-y stop-y step-y)]
                                 [vx (in-range start-x stop-x step-x)])
                            (yield (list (sdf-clip evaluator vx vy vz radius) vx vy vz))))]
                   #:when (sdf-handle-is-valid? (car clip)))
          (let* ([subtree (sdf-quote (car clip))]
                 [center (cdr clip)]
                 [vox-low (vec-max model-low (vec- center half-voxel))]
                 [vox-high (vec-min model-high (vec+ center half-voxel))])
            (list vox-low vox-high subtree)))]
       [(subtrees) (profile-scope "extract subtrees"
                                  (extract-subtrees voxels))])

    (profile-scope
     "group subtrees"

     (for/list ([subtree (in-list subtrees)])
       (cons subtree
             (for/list ([voxel (in-list voxels)]
                        #:when (equal? (caddr voxel) subtree))
               (cons (car voxel) (cadr voxel))))))))


; Compiler strat that divides the model's volume into voxels.  The CSG tree is
; then evaluated for each voxel to remove all branches that cannot affect the
; voxel.
(define (voxel-compiler csgst [voxel-size .25])
  (profile-scope
   "voxel-compiler"
   (let*
       (; Perform transform folding
        [coalesced-tree
         (profile-scope "transform folding"
                        (coalesce csgst))]

        ; Create an evaluator for the model.
        [evaluator
         (profile-scope "evaluator"
                        (sdf-build coalesced-tree))]

        ; Find the model boundaries, and bounded subtrees
        [limits
         (profile-scope "model bounds"
                        (sdf-bounds evaluator))]

        ; Find voxel subtrees.
        [parts (if (sdf-handle-is-valid? evaluator)
                   (profile-scope "find voxel subtrees"
                                  (vox-and-clip limits evaluator voxel-size))
                   null)])

     (profile-scope "glsl generation"
                    (assemble (flatten limits) evaluator parts)))))


; test
(require "csgst.rkt")
