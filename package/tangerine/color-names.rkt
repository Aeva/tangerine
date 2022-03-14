#lang racket/base

; Copyright 2022 Aeva Palecek
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

(require racket/match)
(provide color-by-name)


(define (color-hex c)
  (let ([r (/ (bitwise-and (arithmetic-shift c -16) #xFF) #xFF)]
        [g (/ (bitwise-and (arithmetic-shift c -8) #xFF) #xFF)]
        [b (/ (bitwise-and c #xFF) #xFF)])
    (values r g b)))


(define (color-by-name name)
  (cond
    [(number? name)
     (color-hex name)]

    [(string? name)
     (color-by-name (string->symbol name))]

    [(symbol? name)
     (match name
       ; The color name definitions below are from this page:
       ; https://developer.mozilla.org/en-US/docs/Web/CSS/color_value

       ; CSS Level 1
       ['black (color-hex #x000000)]
       ['silver (color-hex #xc0c0c0)]
       ['gray (color-hex #x808080)]
       ['white (color-hex #xffffff)]
       ['maroon (color-hex #x800000)]
       ['red (color-hex #xff0000)]
       ['purple (color-hex #x800080)]
       ['fuchsia (color-hex #xff00ff)]
       ['green (color-hex #x008000)]
       ['lime (color-hex #x00ff00)]
       ['olive (color-hex #x808000)]
       ['yellow (color-hex #xffff00)]
       ['navy (color-hex #x000080)]
       ['blue (color-hex #x0000ff)]
       ['teal (color-hex #x008080)]
       ['aqua (color-hex #x00ffff)]

       ; CSS Level 2 (Revision 1)
       ['orange (color-hex #xffa500)]

       ; TODO: CSS Color Module Level 3

       ; CSS Color Module Level 4
       ['rebeccapurple (color-hex #x663399)])]))
