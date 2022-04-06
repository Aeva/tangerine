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
(require ffi/unsafe)
(require "ffi.rkt")
(require "csgst.rkt")

(provide sdf-handle-is-valid?
         sdf-build
         sdf-free
         sdf-eval
         sdf-clip
         _HANDLE
         SetTreeEvaluator)


(define _HANDLE (_cpointer/null 'void))

(define-backend SetTreeEvaluator (_fun _HANDLE -> _void))

(define-backend EvalTree (_fun _HANDLE _float _float _float -> _float))
(define-backend ClipTree (_fun _HANDLE _float _float _float _float -> _HANDLE))
(define-backend DiscardTree (_fun _HANDLE -> _void))

(define-backend TreeHasFiniteBounds (_fun _HANDLE -> _bool))

(define-backend MoveTree (_fun _HANDLE _float _float _float -> _void))
(define-backend RotateTree (_fun _HANDLE _float _float _float _float -> _void))
(define-backend AlignTree (_fun _HANDLE _float _float _float -> _void))

(define-backend MakeSphereBrush (_fun _float -> _HANDLE))
(define-backend MakeEllipsoidBrush (_fun _float _float _float -> _HANDLE))
(define-backend MakeBoxBrush (_fun _float _float _float -> _HANDLE))
(define-backend MakeTorusBrush (_fun _float _float -> _HANDLE))
(define-backend MakeCylinderBrush (_fun _float _float -> _HANDLE))

(define-backend MakePlaneOperand (_fun _float _float _float -> _HANDLE))

(define-backend MakeUnionOp (_fun _HANDLE _HANDLE -> _HANDLE))
(define-backend MakeDiffOp (_fun _HANDLE _HANDLE -> _HANDLE))
(define-backend MakeInterOp (_fun _HANDLE _HANDLE -> _HANDLE))

(define-backend MakeBlendUnionOp (_fun _float _HANDLE _HANDLE -> _HANDLE))
(define-backend MakeBlendDiffOp (_fun _float _HANDLE _HANDLE -> _HANDLE))
(define-backend MakeBlendInterOp (_fun _float _HANDLE _HANDLE -> _HANDLE))

(define-backend MakePaint (_fun _float _float _float _HANDLE -> _HANDLE))


; Dispatch SDF creation functions from CSGST expressions.
(define (translate csgst)
  (unless (csg? csgst)
    (error "Expected CSGST expression."))

  (case (car csgst)
    [(move)
     (let*-values ([(x y z subtree) (splat (cdr csgst))]
                   [(evaluator) (translate subtree)])
       (MoveTree evaluator x y z)
       evaluator)]

    [(quat)
     (let*-values ([(x y z w subtree) (splat (cdr csgst))]
                   [(evaluator) (translate subtree)])
       (RotateTree evaluator x y z w)
       evaluator)]

    [(align)
     (let*-values ([(x y z subtree) (splat (cdr csgst))]
                   [(evaluator) (translate subtree)])
       (AlignTree evaluator x y z)
       evaluator)]

    [(paint)
     (let-values ([(red green blue subtree) (splat (cdr csgst))])
       (MakePaint red green blue (translate subtree)))]

    [(sphere)
     (let ([radius (cadr csgst)])
       (MakeSphereBrush radius))]

    [(ellipsoid)
     (let-values ([(radipode-x radipode-y radipode-z) (splat (cdr csgst))])
       (apply MakeEllipsoidBrush (list radipode-x radipode-y radipode-z)))]

    [(box)
     (let-values ([(extent-x extent-y extent-z) (splat (cdr csgst))])
       (apply MakeBoxBrush (list extent-x extent-y extent-z)))]

    [(torus)
     (let-values ([(major-radius minor-radius) (splat (cdr csgst))])
       (MakeTorusBrush major-radius minor-radius))]

    [(cylinder)
     (let-values ([(radius extent) (splat (cdr csgst))])
       (MakeCylinderBrush radius extent))]

    [(plane)
     (let-values ([(normal-x normal-y normal-z) (splat (cdr csgst))])
       (MakePlaneOperand normal-x normal-y normal-z))]

    [(union)
     (let ([lhs (translate (cadr csgst))]
           [rhs (translate (caddr csgst))])
       (MakeUnionOp lhs rhs))]

    [(diff)
     (let ([lhs (translate (cadr csgst))]
           [rhs (translate (caddr csgst))])
       (MakeDiffOp lhs rhs))]

    [(inter)
     (let ([lhs (translate (cadr csgst))]
           [rhs (translate (caddr csgst))])
       (MakeInterOp lhs rhs))]

    [(blend-union)
     (let ([threshold (cadr csgst)]
           [lhs (translate (caddr csgst))]
           [rhs (translate (cadddr csgst))])
       (MakeBlendUnionOp threshold lhs rhs))]

    [(blend-diff)
     (let ([threshold (cadr csgst)]
           [lhs (translate (caddr csgst))]
           [rhs (translate (cadddr csgst))])
       (MakeBlendDiffOp threshold lhs rhs))]

    [(blend-inter)
     (let ([threshold (cadr csgst)]
           [lhs (translate (caddr csgst))]
           [rhs (translate (cadddr csgst))])
       (MakeBlendInterOp threshold lhs rhs))]))


; Returns #t if the s-expression wraps a SDF evaluation tree handle.
(define (sdf-handle? handle)
  (eq? 'sdf-handle (car handle)))


; Returns #t the provided SDF evaluation tree represents a live tree, otherwise
; return false.  This will raise an error if the input is not an SDF handle.
(define (sdf-handle-is-valid? handle)
  (unless (sdf-handle? handle)
    (error "Expected SDF tree handle."))
  (not (eq? (cdr handle) null)))


; Translates a valid csgst expression into an executable SDF tree, and returns
; the handle for later deletion or evaluation.  If the backend is unavailable, then
; a null handle will be returned.
(define (sdf-build csgst)
  (cons 'sdf-handle
        (if (not (ffi-obj-ref 'MakeSphereBrush (ffi-lib #f) (λ () #f)))
            null
            (let ([evaluator (translate csgst)])
              (if (TreeHasFiniteBounds evaluator)
                  evaluator
                  (begin
                    (DiscardTree evaluator)
                    (error "Cannot compile CSG tree with infinite area.")
                    null))))))


; Delete the tree associated with the provided SDF handle.  This will crash if
; called more than once per handle.  If the handle is null, then this will do nothing.
(define (sdf-free handle)
  (when (sdf-handle-is-valid? handle)
    (DiscardTree (cdr handle)))
  (void))


; Executor for lazy deletion of SDF trees created by sdf-clip.
(define sdf-executor (make-will-executor))
(void
 (thread
       (λ ()
         (let loop ()
           (will-execute sdf-executor)
           (loop)))))


; Register a SDF tree for lazy deletion.
(define (lazy-sdf-free handle)
  (when (sdf-handle-is-valid? handle)
    (will-register sdf-executor handle (λ (handle) (DiscardTree (cdr handle)))))
  (void))


; Evaluate the SDF tree for a given point.  If the handle is null, then this will throw an error.
(define (sdf-eval handle x y z)
  (unless (sdf-handle-is-valid? handle)
    (error "Expected valid SDF handle."))
  (EvalTree (cdr handle) x y z))


; Evaluate the SDF tree for a given point and radius to produce a new SDF tree containing only
; nodes which can contribute to the specified region.  If an invalid tree was returned, then
; no SDF nodes could contribute to the specified region.
(define (sdf-clip handle x y z radius)
  (unless (sdf-handle-is-valid? handle)
    (error "Expected valid SDF handle."))
  (let* ([clip-ptr (ClipTree (cdr handle) x y z radius)]
         [new-handle (cons 'sdf-handle (or clip-ptr null))])
    (lazy-sdf-free new-handle)
    new-handle))
