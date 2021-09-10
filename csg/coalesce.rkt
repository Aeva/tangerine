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


(define (peel csgst [transforms null])
  (if (transform? csgst)
      (let* ([pivot (- (length csgst) 1)]
             [trans (take csgst pivot)]
             [next (list-ref csgst pivot)])
        (peel next (append transforms (list trans))))
      (values transforms csgst)))


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


(define (recombine top . next)
  (if (null? next)
      top
      (append top (list (apply recombine next)))))


(define (coalesce csgst)
  (let-values ([(transforms subtree) (peel csgst)])
    (if (null? transforms)
        subtree
        (let ([folds
               (for/list ([group (in-list (groups transforms))])
                 (if (translation? (car group))
                     (fold-translations group)
                     (fold-rotations group)))])
          (if (eq? (length folds) 1)
              (append (car folds) (list subtree))
              (apply recombine (append folds (list subtree))))))))
