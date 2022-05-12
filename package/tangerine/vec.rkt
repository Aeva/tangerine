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


(require racket/list)
(require racket/math)


(provide swiz
         vec4
         vec3
         vec2
         mat4
         mat4-identity
         vec4?
         vec3?
         vec2?
         mat4?
         vec+
         vec-
         vec*
         vec/
         vec/↑
         vec=
         vec-min
         vec-max
         vec-floor
         vec-ceil
         vec-round
         dot
         vec-len
         normalize
         distance
         cross
         invert-mat4
         transpose-mat4
         translate-mat4
         rotate-x-mat4
         rotate-y-mat4
         rotate-z-mat4
         quat->mat4
         mat*
         quat*
         apply-matrix
         quat-rotate
         rcp
         lerp)


(define (vec size . params)
  (if (and
       (number? (car params))
       (null? (cdr params)))
      (for/list ([i (in-range size)])
        (car params))
      (let ([params (append (flatten params) (list 0. 0. 0. 0.))])
        (take params size))))


(define (vec4 . params)
  (apply vec (cons 4 params)))


(define (vec3 . params)
  (apply vec (cons 3 params)))


(define (vec2 . params)
  (apply vec (cons 2 params)))


; Column-major 4x4 matrix.
(define (mat4 ax bx cx dx
              ay by cy dy
              az bz cz dz
              aw bw cw dw)
  (list (vec4 ax ay az aw)
        (vec4 bx by bz bw)
        (vec4 cx cy cz cw)
        (vec4 dx dy dz dw)))


(define (mat4-identity)
  (mat4 1. 0. 0. 0.
        0. 1. 0. 0.
        0. 0. 1. 0.
        0. 0. 0. 1.))


(define (vec4? thing)
  (and (list? thing)
       (eq? 4 (length thing))
       (number? (car thing))
       (number? (cadr thing))
       (number? (caddr thing))
       (number? (cadddr thing))))


(define (vec3? thing)
  (and (list? thing)
       (eq? 3 (length thing))
       (number? (car thing))
       (number? (cadr thing))
       (number? (caddr thing))))


(define (vec2? thing)
  (and (list? thing)
       (eq? 2 (length thing))
       (number? (car thing))
       (number? (cadr thing))))


(define (mat4? thing)
  (and (list? thing)
       (eq? 4 (length thing))
       (vec4? (car thing))
       (vec4? (cadr thing))
       (vec4? (caddr thing))
       (vec4? (cadddr thing))))


(define (swiz vec . channels)
  (for/list ([c (in-list channels)])
    (list-ref vec c)))


(define (vec-op op)
  (define (outter lhs rhs . others)
    (let ([inner
           (λ (lhs rhs)
             (cond [(number? lhs)
                    (for/list ([num (in-list rhs)])
                      (op lhs num))]
                   [(number? rhs)
                    (for/list ([num (in-list lhs)])
                      (op num rhs))]
                   [else
                    (for/list ([i (in-range (min (length lhs) (length rhs)))])
                      (let ([a (list-ref lhs i)]
                            [b (list-ref rhs i)])
                        (op a b)))]))])
      (let ([ret (inner lhs rhs)])
        (if (null? others)
            ret
            (apply outter (cons ret others))))))
  outter)


(define vec+ (vec-op +))


(define vec- (vec-op -))


(define vec* (vec-op *))


(define vec/ (vec-op /))


; Divide and round up.
(define vec/↑ (vec-op (lambda (n d) (ceiling (/ n d)))))


(define (vec= lhs rhs)
  (and
   (= (length lhs) (length rhs))
   (for/and ([a (in-list lhs)]
             [b (in-list rhs)])
     (= a b))))


(define vec-min (vec-op min))


(define vec-max (vec-op max))


(define (vec-floor vec)
  (map floor vec))


(define (vec-ceil vec)
  (map ceiling vec))


(define (vec-round vec)
  (map round vec))


(define (dot lhs rhs)
  (apply + (vec* lhs rhs)))


(define (vec-len vec)
  (sqrt (dot vec vec)))


(define (normalize vec)
  (vec/ vec (dot vec vec)))


(define (distance lhs rhs)
  (if (and (number? lhs)
           (number? rhs))
      (abs (- lhs rhs))
      (vec-len (vec- lhs rhs))))


(define (cross lhs rhs)
  (let* ([sign (vec2 1. -1.)])
    (vec3
     (dot (vec* sign (swiz lhs 1 2)) (swiz rhs 2 1))
     (dot (vec* sign (swiz lhs 2 0)) (swiz rhs 0 2))
     (dot (vec* sign (swiz lhs 0 1)) (swiz rhs 1 0)))))


(define (mat4-ref matrix x y)
  (list-ref (list-ref matrix x) y))


(define (invert-mat4 matrix)
  (let* ([cell (λ (x y) (mat4-ref matrix x y))]
         [coef-00 (- (* (cell 2 2) (cell 3 3)) (* (cell 3 2) (cell 2 3)))]
         [coef-02 (- (* (cell 1 2) (cell 3 3)) (* (cell 3 2) (cell 1 3)))]
         [coef-03 (- (* (cell 1 2) (cell 2 3)) (* (cell 2 2) (cell 1 3)))]
         [coef-04 (- (* (cell 2 1) (cell 3 3)) (* (cell 3 1) (cell 2 3)))]
         [coef-06 (- (* (cell 1 1) (cell 3 3)) (* (cell 3 1) (cell 1 3)))]
         [coef-07 (- (* (cell 1 1) (cell 2 3)) (* (cell 2 1) (cell 1 3)))]
         [coef-08 (- (* (cell 2 1) (cell 3 2)) (* (cell 3 1) (cell 2 2)))]
         [coef-10 (- (* (cell 1 1) (cell 3 2)) (* (cell 3 1) (cell 1 2)))]
         [coef-11 (- (* (cell 1 1) (cell 2 2)) (* (cell 2 1) (cell 1 2)))]
         [coef-12 (- (* (cell 2 0) (cell 3 3)) (* (cell 3 0) (cell 2 3)))]
         [coef-14 (- (* (cell 1 0) (cell 3 3)) (* (cell 3 0) (cell 1 3)))]
         [coef-15 (- (* (cell 1 0) (cell 2 3)) (* (cell 2 0) (cell 1 3)))]
         [coef-16 (- (* (cell 2 0) (cell 3 2)) (* (cell 3 0) (cell 2 2)))]
         [coef-18 (- (* (cell 1 0) (cell 3 2)) (* (cell 3 0) (cell 1 2)))]
         [coef-19 (- (* (cell 1 0) (cell 2 2)) (* (cell 2 0) (cell 1 2)))]
         [coef-20 (- (* (cell 2 0) (cell 3 1)) (* (cell 3 0) (cell 2 1)))]
         [coef-22 (- (* (cell 1 0) (cell 3 1)) (* (cell 3 0) (cell 1 1)))]
         [coef-23 (- (* (cell 1 0) (cell 2 1)) (* (cell 2 0) (cell 1 1)))]
         [fac-0 (vec4 coef-00 coef-00 coef-02 coef-03)]
         [fac-1 (vec4 coef-04 coef-04 coef-06 coef-07)]
         [fac-2 (vec4 coef-08 coef-08 coef-10 coef-11)]
         [fac-3 (vec4 coef-12 coef-12 coef-14 coef-15)]
         [fac-4 (vec4 coef-16 coef-16 coef-18 coef-19)]
         [fac-5 (vec4 coef-20 coef-20 coef-22 coef-23)]
         [vec-0 (vec4 (cell 1 0) (vec3 (cell 0 0)))]
         [vec-1 (vec4 (cell 1 1) (vec3 (cell 0 1)))]
         [vec-2 (vec4 (cell 1 2) (vec3 (cell 0 2)))]
         [vec-3 (vec4 (cell 1 3) (vec3 (cell 0 3)))]
         [inv-0 (vec+ (vec- (vec* vec-1 fac-0) (vec* vec-2 fac-1)) (vec* vec-3 fac-2))]
         [inv-1 (vec+ (vec- (vec* vec-0 fac-0) (vec* vec-2 fac-3)) (vec* vec-3 fac-4))]
         [inv-2 (vec+ (vec- (vec* vec-0 fac-1) (vec* vec-1 fac-3)) (vec* vec-3 fac-5))]
         [inv-3 (vec+ (vec- (vec* vec-0 fac-2) (vec* vec-1 fac-4)) (vec* vec-2 fac-5))]
         [sign-a (vec4 1 -1 1 -1)]
         [sign-b (vec4 -1 1 -1 1)]
         [inverse (list (vec* inv-0 sign-a)
                        (vec* inv-1 sign-b)
                        (vec* inv-2 sign-a)
                        (vec* inv-3 sign-b))]
         [row-0 (car (transpose-mat4 inverse))]
         [determinant (dot (car matrix) row-0)])
    (for/list ([col (in-list inverse)])
      (for/list ([n (in-list col)])
        (/ n determinant)))))


(define (transpose-mat4 matrix)
  (apply mat4 (append* matrix)))


(define (translate-mat4 x y z)
  (mat4 1. 0. 0. x
        0. 1. 0. y
        0. 0. 1. z
        0. 0. 0. 1.))


(define (rotate-x-mat4 degrees)
  (let* ([radians (degrees->radians degrees)]
         [cos-r (cos radians)]
         [sin-r (sin radians)]
         [sin-e (* -1 sin-r)])
    (mat4 1.    0.    0.    0.
          0.    cos-r sin-r 0.
          0.    sin-e cos-r 0.
          0.    0.    0.    1.)))


(define (rotate-y-mat4 degrees)
  (let* ([radians (degrees->radians degrees)]
         [cos-r (cos radians)]
         [sin-r (sin radians)]
         [sin-e (* -1 sin-r)])
    (mat4 cos-r 0.    sin-e 0.
          0.    1.    0.    0.
          sin-r 0.    cos-r 0.
          0.    0.    0.    1.)))


(define (rotate-z-mat4 degrees)
  (let* ([radians (degrees->radians degrees)]
         [cos-r (cos radians)]
         [sin-r (sin radians)]
         [sin-e (* -1 cos-r)])
    (mat4 cos-r sin-e 0.    0.
          sin-r cos-r 0.    0.
          0.    0.    1.    0.
          0.    0.    0.    1.)))


(define (quat->mat4 quat)
  (let-values ([(x y z w) (apply values quat)])
    (let* ([x2 (* x 2)]
           [y2 (* y 2)]
           [z2 (* z 2)]
           [xx (* x x2)]
           [yx (* y x2)]
           [yy (* y y2)]
           [zx (* z x2)]
           [zy (* z y2)]
           [zz (* z z2)]
           [wx (* w x2)]
           [wy (* w y2)]
           [wz (* w z2)])
      (mat4 (- 1 yy zz) (- yx wz)   (+ zx wy)   0.
            (+ yx wz)   (- 1 xx zz) (- zy wx)   0.
            (- zx wy)   (+ zy wx)   (- 1 xx yy) 0.
            0.          0.          0.          1.))))


(define (mat* lhs rhs)
  (let-values ([(la lb lc ld) (apply values lhs)]
               [(ra rb rc rd) (apply values (transpose-mat4 rhs))])
    (mat4 (dot la ra) (dot lb ra) (dot lc ra) (dot ld ra)
          (dot la rb) (dot lb rb) (dot lc rb) (dot ld rb)
          (dot la rc) (dot lb rc) (dot lc rc) (dot ld rc)
          (dot la rd) (dot lb rd) (dot lc rd) (dot ld rd))))


(define (quat* lhs rhs)
  (let ([sign-v (vec4 1 1 1 -1)]
        [sign-w (vec4 -1 -1 -1 1)])
    (vec4
     (dot (swiz lhs 3 0 1 2) (vec* (swiz rhs 0 3 2 1) sign-v))
     (dot (swiz lhs 3 1 2 0) (vec* (swiz rhs 1 3 0 2) sign-v))
     (dot (swiz lhs 3 2 0 1) (vec* (swiz rhs 2 3 1 0) sign-v))
     (dot lhs (vec* rhs sign-w)))))


(define (apply-matrix point matrix)
  (let* ([matrix (invert-mat4 matrix)]
         [point (vec4 point 1.0)]
         [point (list point point point point)] ; °ω°
         [point (car (mat* point matrix))]
         [w (list-ref point 3)]
         [xyz (map (λ (x) (/ x w)) (take point 3))])
    xyz))


(define (quat-rotate point quat)
  (let* ([sign (vec2 1. -1.)]
         [tmp (vec4
               (dot point (vec* (swiz sign 0 1 0) (swiz quat 3 2 1)))
               (dot point (vec* (swiz sign 0 0 1) (swiz quat 2 3 0)))
               (dot point (vec* (swiz sign 1 0 0) (swiz quat 1 0 3)))
               (dot point (vec* (swiz sign 1 1 1) (swiz quat 0 1 2))))])
    (vec3
     (dot tmp (vec* (swiz sign 0 1 0 1) (swiz quat 3 2 1 0)))
     (dot tmp (vec* (swiz sign 0 0 1 1) (swiz quat 2 3 0 1)))
     (dot tmp (vec* (swiz sign 1 0 0 1) (swiz quat 1 0 3 2))))))


(define (rcp vec)
  (if (number? vec)
      (/ 1.0 vec)
      (vec/ 1.0 vec)))


(define (lerp lhs rhs alpha)
  (cond
    [(and
      (number? lhs)
      (number? rhs)
      (number? alpha))
     (let ([inv-alpha (- 1.0 alpha)])
           (+ (* lhs inv-alpha) (* rhs alpha)))]
    [(and
      (list? lhs)
      (list? rhs)
      (list? alpha))
     (for/list ([lhs lhs]
                [rhs rhs]
                [alpha alpha])
       (lerp lhs rhs alpha))]
    [else
     (lerp
      (if (list? lhs) lhs (vec4 lhs))
      (if (list? rhs) rhs (vec4 rhs))
      (if (list? alpha) alpha (vec4 alpha)))]))
