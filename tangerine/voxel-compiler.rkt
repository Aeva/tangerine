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
(require ffi/unsafe)
(require ffi/unsafe/define)
(require "coalesce.rkt")
(require "compiler-common.rkt")
(require "eval.rkt")
(require "vec.rkt")
(require "profiling.rkt")

(provide voxel-compiler)


(define-ffi-definer define-backend (ffi-lib #f) #:default-make-fail make-not-available)
(define _HANDLE (_cpointer/null 'void))
(define-backend VoxelFinder (_fun _HANDLE _float -> _scheme))

(define (voxel-finder handle voxel-size)
  (unless (sdf-handle-is-valid? handle)
    (error "Expected valid SDF handle."))
  (VoxelFinder (cdr handle) voxel-size))


(define (splat args)
  (apply values args))


; Extract a list of subtrees from a list of AABBs.
(define (extract-subtrees bounds)
  (remove-duplicates
   (for/list ([aabb (in-list bounds)])
     (caddr aabb))))


(define (vox-and-clip evaluator voxel-size)
  (let*-values
      ([(voxels) (voxel-finder evaluator voxel-size)]
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

        ; Find voxel subtrees.
        [parts (if (sdf-handle-is-valid? evaluator)
                   (profile-scope "find voxel subtrees"
                                  (vox-and-clip evaluator voxel-size))
                   null)]

        ; Generate GLSL.
        [clusters
         (profile-scope "generate glsl clusters"
                    (assemble parts))])

     (values evaluator clusters))))


; test
(require "csgst.rkt")
