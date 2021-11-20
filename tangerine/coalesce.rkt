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
(require "csgst.rkt")
(require "vec.rkt")

(provide coalesce)


(define (translation? csgst)
  (eq? (car csgst) 'move))


(define (rotation? csgst)
  (eq? (car csgst) 'quat))


(define (matrix? csgst)
  (eq? (car csgst) 'mat4))


(define (groups transforms [pending null] [mode #f])
  (if (null? transforms)
      (if (null? pending)
          null
          (list pending))
      (let ([top (car transforms)]
            [next (cdr transforms)])
        (if (null? pending)
            (groups next (list top) (translation? top))
            (if (eq? (translation? top) mode)
                (groups next (append pending (list top)) mode)
                (append (list pending)
                        (groups next (list top) (translation? top))))))))


(define (fold-translations translations)
  (let ([offsets (for/list ([trans (in-list translations)])
                   (apply vec3 (cdr trans)))])
    (cons 'move
          (for/fold ([offset (car offsets)])
                    ([next (in-list (cdr offsets))])
            (vec+ offset next)))))


(define (fold-rotations rotations)
  (let ([turns (for/list ([quat (in-list rotations)])
                   (apply vec4 (cdr quat)))])
    (cons 'quat
          (for/fold ([turn (car turns)])
                    ([next (in-list (cdr turns))])
            (quat* turn next)))))


(define (peel-1 csgst)
  (let ([pivot (- (length csgst) 1)])
    (values (take csgst pivot)
            (list-ref csgst pivot))))


(define (peel-2 csgst)
  (let ([pivot (- (length csgst) 2)])
    (values (take csgst pivot)
            (list-ref csgst pivot)
            (list-ref csgst (+ 1 pivot)))))


(define (translate->matrix partial)
  (let-values ([(node x y z) (apply values partial)])
    (let ([inv-translation (vec* -1 (vec3 x y z))])
      (apply translate-mat4 inv-translation))))


(define (quat->matrix partial)
  (let-values ([(node x y z w) (apply values partial)])
    (transpose-mat4 (quat->mat4 (list x y z w)))))


(define (->matrix partial)
  (cond [(translation? partial)
         (translate->matrix partial)]
        [(rotation? partial)
         (quat->matrix partial)]))


(define (recombine csgst transforms)
  (if (null? transforms)
      csgst
      (let* ([folds
              (for/list ([group (in-list (groups transforms))])
                (if (translation? (car group))
                    (fold-translations group)
                    (fold-rotations group)))]
             [matrix
              (for/fold ([acc (mat4-identity)])
                        ([matrix (in-list (map ->matrix folds))])
                (mat* acc matrix))])
        (if (and (eq? 1 (length folds))
                 (translation? (car folds)))
            (append (car folds) (list csgst))
            (append (list 'mat4 matrix) (list csgst))))))


(define (coalesce csgst [transforms null])
  (cond [(transform? csgst)
         (let-values ([(tran subtree) (peel-1 csgst)])
           (if (null? transforms)
               (coalesce subtree (list tran))
               (coalesce subtree (append transforms (list tran)))))]

        [(operator? csgst)
         (let-values ([(operator lhs rhs) (peel-2 csgst)])
           (append operator (list (coalesce lhs transforms) (coalesce rhs transforms))))] ; fold through operators up to brushes
           ;(recombine (append operator (list (coalesce lhs) (coalesce rhs))) transforms))] ; fold up to operators, then fold operands

        [(paint? csgst)
         (let-values ([(annotation subtree) (peel-1 csgst)])
           (append annotation (list (coalesce subtree transforms))))] ; fold through annotations

        [else
         (recombine csgst transforms)]))
