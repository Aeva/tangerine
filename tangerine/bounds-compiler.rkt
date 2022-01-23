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

(require "bounds.rkt")
(require "coalesce.rkt")
(require "compiler-common.rkt")
(require "eval.rkt")

(provide bounds-compiler)


; Compiler strat that attempts to estimate the bounding boxes of all useful
; static permutations of a CSG tree.  This allows for overlap under the theory
; that unions can be made cheap by relying on the z-buffer to sort it out.
; In practice this has problems with cutaways of unions of objects with
; different materials, normal generation, and perform poorly with round objects
; with lots of cuts eg gears.
(define (bounds-compiler csgst)
  (let*-values
      (; Perform transform folding
       [(coalesced-tree) (coalesce csgst)]

       ; Find the model boundaries, and bounded subtrees
       [(limits parts) (segments coalesced-tree)]

       ; Create an evaluator for the model.
       [(evaluator) (sdf-build coalesced-tree)])

    (assemble limits evaluator parts)))
