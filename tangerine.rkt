#lang racket/gui

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

(require ffi/unsafe
         ffi/unsafe/define)

; Functions provided by the backend dll.
(define-ffi-definer define-backend (ffi-lib "tangerine.dll"))
(define-backend Setup (_fun -> _int))
(define-backend Resize (_fun _int _int -> _void))
(define-backend NewShader (_fun _string/utf-8 -> _void))
(define-backend Shutdown (_fun -> _void))

; Access the GL canvas's gl context.
(define (get-gl-context)
  (send (send canvas get-dc) get-gl-context))

; The main window.
(define frame
  (new
   (class frame% (super-new)
     (define/augment (on-close)
       (send (get-gl-context) call-as-current Shutdown)))
   [label "Tangerine"]))

; The OpenGL context will be created the core profile and no depth buffer.
(define gl-config (new gl-config%))
(send gl-config set-legacy? #f)
(send gl-config set-depth-size 0)
(send gl-config set-sync-swap #t)

; Backend error handling.
(define (verify hint gl-ctx thunk)
  (let ([status (send gl-ctx call-as-current thunk)])
    (when (eq? status 1)
      (display (~a "Fatal error in " hint "."))
      (exit))))

; Create the OpenGL canvas widget.
(define canvas
  (new canvas%
       [parent frame]
       [style (list 'gl 'no-autoclear)]
       [gl-config gl-config]
       [min-width 512]
       [min-height 512]
       [paint-callback
        (lambda (canvas dc)
          (let-values ([(width height) (send canvas get-gl-client-size)])
            (Resize width height)))]))

; Show the main window.
(send frame show #t)

; Initialize OpenGL.
(verify "Setup" (get-gl-context) Setup)

; Send a new program.
(NewShader "float SceneDist(vec3 Point)\n{\n\treturn SphereDist(Point, 1.8);\n}\n")
