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


(define (glsl-vec3 vec)
  (let-values ([(x y z) (apply values vec)])
    @~a{vec3(@x, @y, @z)}))


(define (compile csgst)
  (let ([parts (segments csgst)])
    (for/list ([part (in-list (segments csgst))])
      (let* ([subtree (coalesce (car part))]
             [bounds (cdr part)]
             [count (length bounds)])
        (cons
         (length bounds)
         (cons
          (string->bytes/utf-8
           (string-join
            (list
             "float ClusterDist(vec3 Point)"
             "{"
             (~a "\treturn " (eval-dist subtree) ";")
             "}\n")
            "\n"))
          (string->bytes/utf-8
           (string-join
            (flatten
             (list
              @~a{const uint ClusterCount = @count;}
              "AABB ClusterData[ClusterCount] = \\"
              "{"
              (string-join
               (for/list ([bound (in-list bounds)])
                 (let* ([low (car bound)]
                        [high (cdr bound)]
                        [extent (vec* 0.5 (vec- high low))]
                        [center (vec+ low extent)])
                   {~a "\t" @~a{AABB(@(glsl-vec3 center), @(glsl-vec3 extent))}}
                   )) ",\n")
              "};\n"))
            "\n"))))))))


(define (renderer-load-and-process-model path-str)
  (let ([path (string->path path-str)])
    (dynamic-rerequire path)
    (let ([emit-glsl (dynamic-require path 'emit-glsl)])
      (emit-glsl))))
