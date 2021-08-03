#lang racket/base

(require "sdf.rkt")

(provide emit-glsl)

(define (emit-glsl)
  (scene
   (union
    (smooth-cut
     0.5
     (sphere 1.8)
     (trans .9 -.5 0.
            (sphere 1.6)))
    (trans .9 -.5 0.
            (sphere 0.8)))))
