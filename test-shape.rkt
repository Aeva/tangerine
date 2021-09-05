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

(require "csg.rkt")
(require "csg/bounds.rkt")
(require "csg/bpy-export.rkt")

; compatibility stuff
(define scene compile)
(define trans move)
(define trans-x move-x)
(define trans-y move-y)
(define trans-z move-z)
(define cut diff)
(define (smooth-union threshold lhs rhs) (blend union threshold lhs rhs))
(define (smooth-inter threshold lhs rhs) (blend inter threshold lhs rhs))
(define (smooth-cut threshold lhs rhs) (blend cut threshold lhs rhs))
(define (quat-rot-x deg) `(x ,deg))
(define (quat-rot-y deg) `(y ,deg))
(define (quat-rot-z deg) `(z ,deg))
(define (rotate params child)
  (let ([axis (car params)]
        [degrees (cadr params)])
    (case axis
      [(x) (rotate-x degrees child)]
       [(y) (rotate-y degrees child)]
       [(z) (rotate-z degrees child)])))


(provide emit-glsl)


(define box-test
  (cut
    (cube 2.3)
    (sphere 3.0)))


(define (window-negative x)
  (trans-x x (box 2. 1. 4.)))


(define (window-positive x alpha)
  (define (sash y z)
    (trans 0. y z
           (union
            (union
             (cut
              (box 2. 0.125 2.)
              (box 1.8 .5 1.8))
             (box 0.05125 0.05125 2.))
            (box 2.0 0.05125 0.05125))))
  (trans-x x
           (union
            (union
             (union
              (sash 0.05125 1.0)
              (sash -0.05125
                    (+ (* -1.0 (- 1.0 alpha))
                       (* 0.8 alpha))))
             (trans-z -2.05
                      (box 2.6 1.0 0.1)))
            (cut
             (trans-z -.1
                      (box 2.4 .7 4.6))
             (box 2. 1. 4.)))))


(define (wall width . params)
  (define tree
    (box width 0.5 10.0))
  (for ([param params])
    (let* ([param-x (car param)]
           [shape (window-negative param-x)])
      (set! tree
            (cut tree
                 shape))))
  (for ([param params])
    (let* ([param-x (car param)]
           [param-a (cadr param)]
           [shape (window-positive param-x param-a)])
      (set! tree
            (union tree
                   shape))))
  tree)


(define (hole-punch tree)
  (for ([n (in-range 6)])
    (let* ([x (+ (random -5 5) (random))]
           [z (+ (random -5 5) (random))]
           [r1 (+ (random 2 4) (random))]
           [r2 (- r1 0.2)]
           [r3 (* r2 0.1)]
           [e (+ 0.01 (* (random) 0.5))])
    (set! tree
          (union
           (cut tree
                (trans x 0 z
                       (sphere r1)))
           (trans x 0 z
                  (cut
                   (inter
                    (sphere r2)
                    (box r2 e r2))
                   (inter
                    (trans-z r3
                             (sphere r2))
                    (trans-z (* -1. r3)
                             (sphere r2)))))))))
  tree)


;(define (emit-glsl)
;  (scene
;   (hole-punch
;    (wall 10.0
;          '(-3.0 0.0)
;          '(0.0 0.3)
;          '(3.0 0.8)))))


(define (tire)
  (define (tread deg)
    (rotate
     (quat-rot-x deg)
     (trans-z 0.75
              (rotate
               (quat-rot-z 45.)
               (cut
                (box 1. 1. 1.)
                (trans 0.125 0.125 0.125
                       (box 1. 1. 1.)))))))
  (define tree (tread 0.))
  (let ([step 20])
    (for ([deg (in-range step 360 step)])
      (set! tree
            (union
             tree
             (tread deg)))))
  (cut
   (smooth-union 0.05
    (smooth-inter 0.1
                  (rotate
                   (quat-rot-y 90.)
                   (smooth-union .5
                                 (trans-z .24
                                          (torus 2.6 .8))
                                 (trans-z -0.24
                                          (torus 2.6 .8))))
                  (sphere 2.45))
    (cut
     (inter
      (sphere 2.55)
      tree)
     (union
      (trans-x 2.2
               (sphere 4.))
      (trans-x -2.2
               (sphere 4.)))))
   
   (sphere 1.6)))


(define (tire-cut)
  (rotate-y 90.
            (cylinder 2.75 1.5)))


(define (spin-steps rotate-n steps stamp [csg-op union])
  (let ([step (/ 360. steps)]
        [acc stamp])
        (for ([deg (in-range step 360 step)])
          (set! acc
                (csg-op
                 acc
                 (rotate-n deg stamp))))
        acc))


(define (nut width depth)
  (spin-steps rotate-z 3. (box (* width 2.) width depth) inter))


(define (hub)
  (let* ([outset 0.55]
         [diameter 1.25]
         [lip 0.075]
         [lip-blend (/ lip 3.0)]
         [inner-diameter (- diameter (/ lip 2.))]
         [cuts-radius (/ inner-diameter 2.5)]
         [rim (torus diameter lip)]
         [plate (ellipsoid inner-diameter inner-diameter .2)]
         [edge-cut-limit (cylinder (- inner-diameter lip lip-blend) .2)]
         [edge-cut-stamp (trans-y cuts-radius (ellipsoid .4 .2 .2))]
         [edge-cuts (inter edge-cut-limit (spin-steps rotate-z 6. edge-cut-stamp))]
         [recess (cylinder .7 .2)]
         [stud-length .07]
         [stud-diameter 0.06]
         [stud (trans-z (/ stud-length 2.) (cylinder stud-diameter stud-length))]
         [nut-width .1]
         [nut-depth (/ nut-width 2.)]
         [lug-nut (trans-z (* nut-depth 0.52)  (rotate-z (* (random) 120.) (nut nut-width nut-depth)))]
         [lug-nutz (spin-steps rotate-z 5. (trans-x .2 (union stud lug-nut)))])
    (trans-x
     outset
     (rotate-y
      90.
      (union
       (smooth-union lip-blend
                     rim
                     (cut
                      (smooth-cut .05
                                  plate
                                  (trans-z .1 recess))
                      edge-cuts))
       lug-nutz)))))


(define (wheel [angle 0.])
  (let ([shape (union
                (tire)
                (hub))])
    (if (eq? angle 0.)
        shape
        (rotate-z angle shape))))


(define (gallery tree)
  (let ([lhs (trans-x 1.
                      (rotate-z -90.
                                tree))]
        [rhs (trans-x -1.3
                      (rotate-z -10.
                                tree))])
    (trans-y -8.
             (union lhs rhs))))


(define (emit-glsl)
  (compile
   (gallery (wheel))))


;(display
; (compile-bpy (gallery (wheel))))
