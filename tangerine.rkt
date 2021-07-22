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
(define-backend Render (_fun _double _int _int -> _int))
(define-backend Shutdown (_fun -> _void))

; Access the GL canvas's gl context.
(define (get-gl-context)
  (send (send canvas get-dc) get-gl-context))

; The main window.
(define frame
  (new
   (class frame% (super-new)
     (define/augment (on-close)
       (send (get-gl-context) call-as-current Shutdown)
       (send heart-beat stop)))
   [label "Racket With External OpenGL Example"]))

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
       [min-width 200]
       [min-height 200]))

; Show the main window.
(send frame show #t)

; Initialize OpenGL.
(verify "Setup" (get-gl-context) Setup)

; Frame rendering callback.
(define (render-callback)
  (define (render-inner)
    (let-values ([(width height) (send canvas get-gl-client-size)]
                 [(current-time) (current-inexact-milliseconds)])
      (Render current-time width height)))
  
  (define (on-paint canvas)
    (let ([gl-ctx (get-gl-context)])
      (verify "Render" gl-ctx render-inner)
      (send gl-ctx swap-buffers)))
  
  (send canvas refresh-now on-paint)
  (yield))

; Frame scheduler.  Render calls glFinish to ensure the correct cadence.
(define heart-beat
  (new timer%
       [notify-callback render-callback]
       [interval 0]))
