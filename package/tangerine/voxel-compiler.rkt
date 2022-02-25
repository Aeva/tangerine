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

(require ffi/unsafe)
(require "ffi.rkt")
(require "coalesce.rkt")
(require "eval.rkt")
(require "profiling.rkt")

(provide voxel-compiler)


(define _HANDLE (_cpointer/null 'void))
(define-backend VoxelCompiler (_fun _HANDLE _float -> _void))


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
                        (sdf-build coalesced-tree))])

     (when (sdf-handle-is-valid? evaluator)
       (profile-scope "find voxel subtrees"
                      (VoxelCompiler (cdr evaluator) voxel-size))))))
