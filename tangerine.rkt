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

(require racket/rerequire
         ffi/unsafe
         ffi/unsafe/define)
(require "renderer.rkt")
;(require "test-shape.rkt")

(define-ffi-definer define-backend (ffi-lib "tangerine"))
(define-backend Resize (_fun _int _int -> _void))
(define-backend LockShaders (_fun -> _void))
(define-backend PostShader (_fun _int _string/utf-8 _string/utf-8 -> _void))
(define-backend UnlockShaders (_fun -> _void))

; Access the GL canvas's gl context.
(define (get-gl-context)
  (send (send canvas get-dc) get-gl-context))

; The main window.
(define frame
  (new
   (class frame% (super-new)
     (define/augment (on-close)
       (send (get-gl-context) call-as-current halt-renderer)))
   [label "Tangerine"]))

; The OpenGL context will be created the core profile and no depth buffer.
(define gl-config (new gl-config%))
(send gl-config set-legacy? #f)
(send gl-config set-depth-size 0)
(send gl-config set-multisample-size 0)
(send gl-config set-sync-swap #t)

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

; Add the menu bar.
(define menu-bar (new menu-bar% [parent frame]))

; File menu and items.
(define file-menu (new menu%
                       [label "&File"]
                       [parent menu-bar]))

(define last-load #f)

(define (load-model-inner path)
  (when path
    (set! last-load #f)
    (dynamic-rerequire path)
    (let ([clusters ((dynamic-require path 'emit-glsl))])
      (LockShaders)
      (for ([cluster (in-list clusters)])
        (PostShader (car cluster) (cadr cluster) (cddr cluster)))
      (UnlockShaders)
      (set! last-load path))))

(define (load-model . args)
  (let ([path (get-file
               "Open"
               frame
               "models"
               #f
               "rkt"
               null
               '(("Racket" "*.rkt")
                 ("Any" "*.*")))])
    (load-model-inner path)))

(define (reload-model . args)
  (load-model-inner last-load))

(define open-action (new menu-item%
                         [label "Open"]
                         [parent file-menu]
                         [callback load-model]
                         [shortcut #\o]))

(define reload-action (new menu-item%
                           [label "Reload"]
                           [parent file-menu]
                           [callback reload-model]
                           [shortcut #\r]))

(new separator-menu-item% [parent file-menu])

(define exit-action (new menu-item%
                         [label "Exit"]
                         [parent file-menu]
                         [callback (λ (arg . args)
                                     (send frame on-exit))]))

; View menu and items.
(define view-menu (new menu%
                       [label "&View"]
                       [parent menu-bar]))

(define fullscreen-state #f)

(define (toggle-fullscreen . args)
  (set! fullscreen-state (not fullscreen-state))
  (send frame fullscreen fullscreen-state))

(define fullscreen-action (new menu-item%
                               [label "Full Screen"]
                               [parent view-menu]
                               [callback toggle-fullscreen]
                               [shortcut #\f]))

; Show the main window.
(send frame show #t)

; Initialize OpenGL.
(send (get-gl-context) call-as-current start-renderer)
