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

(require racket/runtime-path
         racket/place
         ffi/unsafe
         ffi/unsafe/define)

(define-runtime-path backend-path "tangerine.dll")

(provide backend-path
         start-renderer
         halt-renderer)

(define render-thread #f)

(define-ffi-definer define-backend (ffi-lib backend-path))

(define-backend PlatformSupportsAsyncRenderer (_fun -> _bool))

(define-backend Setup (_fun #:in-original-place? #t
                            -> (status : _int)
                            -> (when (eq? status 1)
                                 (error "Failed to initialize OpenGL.\n"))))

(define-backend RenderFrame(_fun #:blocking? #t
                                 #:in-original-place? #t
                                 -> _void))

(define-backend Shutdown (_fun #:blocking? #t
                               #:in-original-place? #t
                               -> _void))

(define (endless)
  (RenderFrame)
  (endless))

(define (start-render-thread)
  (set! render-thread
    (place main-thread
           (endless)))
  (void))

(define (start-renderer)
  (Setup)
  (when (not (PlatformSupportsAsyncRenderer))
    (start-render-thread))
  (void))

(define (halt-renderer)
  (Shutdown))
