#lang racket

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

(provide start-renderer)

(define-ffi-definer define-backend (ffi-lib "tangerine"))

(define-backend PlatformSupportsAsyncRenderer (_fun -> _bool))

(define-backend Setup (_fun _bool
                            -> (status : _int)
                            -> (when (eq? status 1)
                                 (error("Failed to initialize OpenGL.\n")))))

(define-backend RenderFrame(_fun #:blocking? #t
                                 -> _void))

(define-backend PresentFrame (_fun #:blocking? #t
                                   #:in-original-place? #t
                                   -> _void))

(define (endless)
  (RenderFrame)
  (PresentFrame)
  (endless))

(define (start-render-thread)
  (define render-thread
    (place main-thread
           (endless)))
  (void))

(define (start-renderer)
  (let ([async (PlatformSupportsAsyncRenderer)])
    (Setup async)
    (when (not async)
      (start-render-thread))
    (void)))
