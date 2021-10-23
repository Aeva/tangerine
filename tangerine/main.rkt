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
(require racket/string)
(require racket/format)
(require racket/rerequire)
(require ffi/unsafe)
(require ffi/unsafe/define)
(require "csgst.rkt")
(require "bounds.rkt")
(require "coalesce.rkt")
(require "glsl.rkt")
(require "vec.rkt")

(provide compile
         sphere
         ellipsoid
         box
         cube
         torus
         cylinder
         union
         diff
         inter
         blend
         move
         move-x
         move-y
         move-z
         rotate-x
         rotate-y
         rotate-z
         renderer-load-and-process-model)


(define-ffi-definer define-backend (ffi-lib #f) #:default-make-fail make-not-available)
(define-backend EmitShader (_fun _string/utf-8 _string/utf-8 -> _size))
(define-backend EmitBounds (_fun _size _float _float _float _float _float _float -> _void))
(define-backend SetLimitsCallback (_fun _float _float _float _float _float _float -> _void))
(define-backend RacketErrorCallback (_fun _string/utf-8 -> _void))


(define (glsl-vec3 vec)
  (let-values ([(x y z) (apply values vec)])
    @~a{vec3(@x, @y, @z)}))


(define (compile csgst)
  (let-values ([(limits parts) (segments (coalesce csgst))])
    (apply SetLimitsCallback limits)
    (for/list ([part (in-list parts)]
               [subtree-index (in-range (length parts))])
      (let* ([subtree (car part)]
             [bounds (cdr part)]
             [count (length bounds)])
        (append
        (list
         subtree
         (string-join
          (list
           @~a{const uint SubtreeIndex = @subtree-index;}
           "float ClusterDist(vec3 Point)"
           "{"
           (~a "\treturn " (eval-dist subtree) ";")
           "}\n")
          "\n"))
         (for/list ([bound (in-list bounds)])
           (let* ([low (car bound)]
                  [high (cdr bound)]
                  [extent (vec* 0.5 (vec- high low))]
                  [center (vec+ low extent)])
             (flatten (list extent center)))))))))


(define (renderer-load-and-process-model path-str)
  (with-handlers ([exn:fail? (λ (err) (RacketErrorCallback (exn-message err)))])
    (let ([path (string->path path-str)])
      (dynamic-rerequire path)
      (let ([clusters ((dynamic-require path 'emit-glsl))])
        (for ([cluster (in-list clusters)])
          (let* ([tree (~a (car cluster))]
                 [dist (cadr cluster)]
                 [aabbs (cddr cluster)]
                 [index (EmitShader tree dist)])
            (for ([aabb (in-list aabbs)])
              (apply EmitBounds (cons index aabb))))))))
  (void))
