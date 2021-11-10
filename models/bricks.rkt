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


(require tangerine)
(require tangerine/vec)
(require racket/list)
(provide brick-walk)

(define inch 1/12)

(define brick-width (* 61/8 inch))
(define brick-height (* 29/8 inch))
(define brick-depth (* 17/8 inch))
(define mortar (* 3/8 inch))
(define raise (+ brick-depth mortar))


(define brick-h
  (align 0 0 -1 (box brick-width brick-height brick-depth)))


(define brick-v
  (align 0 0 -1 (box brick-height brick-width brick-depth)))


(define brick-c
  (align 0 0 -1 (box brick-height brick-height brick-depth)))


(define horizontal-even
  (union
   (move-x (* 2 inch) brick-h)
   (move-x (* 10 inch) brick-h)))


(define horizontal-odd
  (move-x (* 6 inch) brick-h))


(define vertical-even
  (union
   (move-y (* 2 inch) brick-v)
   (move-y (* 10 inch) brick-v)))


(define vertical-odd
  (move-y (* 6 inch) brick-v))


; Generic form of stack-h and stack-v.  Not intended to be called directly.
(define (stack count even-stamp odd-stamp [even? #t])
  (if (<= count 1)
      (if even? even-stamp odd-stamp)
      (union
       (if even? even-stamp odd-stamp)
       (move-z raise (stack (- count 1) even-stamp odd-stamp (not even?))))))


; Build a stack of bricks aligned to the x axis.
(define (stack-h count [even? #t])
  (stack count horizontal-even horizontal-odd even?))


; Build a stack of bricks aligned to the y axis.
(define (stack-v count [even? #t])
  (stack count vertical-even vertical-odd even?))


; Build a partial stack of half-bicks for corners.
(define (stack-c count [even? #t])
  (define (inner count)
    (if (<= count 2)
        brick-c
        (union brick-c
               (move-z (* raise 2) (inner (- count 2))))))
  (let ([stack (inner (* (floor (/ count 2)) 2))])
    (if even?
        (move-z raise stack)
        stack)))


; Generic form of repeat-h and repeat-v.  Not intended to be called directly.
(define (repeat move-n stack offset width height [even? #t])
  (if (<= width 1)
      (stack height even?)
      (union
       (stack height even?)
       (move-n offset (repeat move-n stack offset (- width 1) height (not even?))))))


; Used by repeat-h and repeat-v to offset the start position
; when the wall runs in the negative direction.
(define (realign offset move-n csgst)
  (if (< offset 0)
      (move-n offset csgst)
      csgst))


; Generate a run of bricks along the x axis.
(define (repeat-h offset width height [even? #t])
  (realign offset move-x
           (repeat move-x stack-h offset width height even?)))


; Generate a run of bricks along the y axis.
(define (repeat-v offset width height [even? #t])
  (realign offset move-y
           (repeat move-y stack-v offset width height even?)))


; Returns 1 if n is positive, and -1 if n is negative.
(define (sign n)
  (if (>= n 0) 1.0 -1.0))


; Generate a brick wall along a path.
(define (brick-walk height start stop . etc)
  (define (find-strokes start stop . corners)
    (cons
     (cons start stop)
     (if (null? corners)
         null
         (apply find-strokes (cons stop corners)))))

  (define (move* xy csgst)
    (move (car xy) (cadr xy) 0 csgst))

  (define controls (cons start (cons stop etc)))
  (define strokes (apply find-strokes controls))
  (define cursor start)
  (define masonry null)
  (define t 0)

  (for ([stroke (in-list strokes)])
    (let* ([start (car stroke)]
           [end (cdr stroke)]
           [x1 (car start)]
           [y1 (cadr start)]
           [x2 (car end)]
           [y2 (cadr end)]
           [dx (round (- x2 x1))]
           [dy (round (- y2 y1))])

      ; Horizontal run.
      (when (0 . < . (abs dx))
        (let* ([run (abs dx)]
               [dir (sign dx)]
               [m-width (+ run (- (* brick-height) mortar))]
               [m-adjust (* (+ (* brick-height .5) (* mortar -.5)) dir -1)]
               [m-height (- brick-height mortar)]
               [m-depth (- (* raise height) (* mortar 1.5))])
          (set! masonry
                (cons
                 (move* cursor
                        (union
                         (repeat-h dir run height (even? t))
                         (move-x m-adjust
                                 (align (* -1 dir) 0 -1
                                        (box m-width m-height m-depth)))))
                 masonry))
          (set! t (+ t run))
          (set! cursor (vec+ cursor (vec2 dx 0)))))

      ; Vertical run.
      (when (0 . < . (abs dy))
        (let* ([run (abs dy)]
               [dir (sign dy)]
               [m-height (+ run (- (* brick-height) mortar))]
               [m-adjust (* (+ (* brick-height .5) (* mortar -.5)) dir -1)]
               [m-width (- brick-height mortar)]
               [m-depth (- (* raise height) (* mortar 1.5))])
          (set! masonry
                (cons
                 (move* cursor
                        (union
                         (repeat-v (sign dy) (abs dy) height (even? t))
                         (move-y m-adjust
                                 (align 0 (* -1 dir) -1
                                        (box m-width m-height m-depth)))))
                 masonry))
          (set! t (+ t (abs dy)))
          (set! cursor (vec+ cursor (vec2 0 dy)))))))
  (let ([final (if (null? etc)
                   stop
                   (last etc))])
    (when (not (vec= start final))
      (set! masonry
            (cons
             (move* start (stack-c height))
             (cons
              (move* final (stack-c height (even? (- t 1))))
              masonry)))))

  (if ((length masonry) . > . 1) (apply union masonry) masonry))
