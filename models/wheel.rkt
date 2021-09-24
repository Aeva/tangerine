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
(provide emit-glsl)


(define (tire)
  (define (tread deg)
    (rotate-x deg
              (move-z 0.75
                      (rotate-z 45
                                (diff
                                 (box 1. 1. 1.)
                                 (move 0.125 0.125 0.125
                                       (box 1. 1. 1.)))))))
  (define tree (tread 0.))
  (let ([step 20])
    (for ([deg (in-range step 360 step)])
      (set! tree
            (union
             tree
             (tread deg)))))
  (diff
   (blend union 0.05
          (blend inter 0.1
                 (rotate-y 90
                           (blend union .5
                                  (move-z .24
                                          (torus 2.6 .8))
                                  (move-z -0.24
                                          (torus 2.6 .8))))
                 (sphere 2.45))
          (diff
           (inter
            (sphere 2.55)
            tree)
           (union
            (move-x 2.2
                    (sphere 4.))
            (move-x -2.2
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
         [edge-cut-stamp (move-y cuts-radius (ellipsoid .4 .2 .2))]
         [edge-cuts (inter edge-cut-limit (spin-steps rotate-z 6. edge-cut-stamp))]
         [recess (cylinder .7 .2)]
         [stud-length .07]
         [stud-diameter 0.06]
         [stud (move-z (/ stud-length 2.) (cylinder stud-diameter stud-length))]
         [nut-width .1]
         [nut-depth (/ nut-width 2.)]
         [lug-nut (move-z (* nut-depth 0.52)  (rotate-z (* (random) 120.) (nut nut-width nut-depth)))]
         [lug-nutz (spin-steps rotate-z 5. (move-x .2 (union stud lug-nut)))])
    (move-x
     outset
     (rotate-y
      90.
      (union
       (blend union lip-blend
              rim
              (diff
               (blend diff .05
                      plate
                      (move-z .1 recess))
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
  (let ([lhs (move-x 1.
                     (rotate-z -90.
                               tree))]
        [rhs (move-x -1.3
                     (rotate-z -10.
                               tree))])
    (union lhs rhs)))


(define (emit-glsl)
  (compile
   (gallery (wheel))))
