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
(provide color-by-name
         color-name?)


(define (color-hex c)
  (let ([r (/ (bitwise-and (arithmetic-shift c -16) #xFF) #xFF)]
        [g (/ (bitwise-and (arithmetic-shift c -8) #xFF) #xFF)]
        [b (/ (bitwise-and c #xFF) #xFF)])
    (values r g b)))


(define (lookup-hex name)
  (cond
    [(string? name) (lookup-hex (string->symbol name))]
    [(symbol? name)
     (match name
       ; The color name definitions below are from this page:
       ; https://developer.mozilla.org/en-US/docs/Web/CSS/color_value

       ; CSS Level 1
       ['black #x000000]
       ['silver #xc0c0c0]
       ['gray #x808080]
       ['white #xffffff]
       ['maroon #x800000]
       ['red #xff0000]
       ['purple #x800080]
       ['fuchsia #xff00ff]
       ['green #x008000]
       ['lime #x00ff00]
       ['olive #x808000]
       ['yellow #xffff00]
       ['navy #x000080]
       ['blue #x0000ff]
       ['teal #x008080]
       ['aqua #x00ffff]

       ; CSS Level 2 (Revision 1)
       ['orange #xffa500]

       ; TODO: CSS Color Module Level 3

       ; CSS Color Module Level 4
       ['rebeccapurple #x663399]
       [_ -1])]
    [else -1]))


(define (color-by-name name)
  (if (number? name)
     (color-hex name)
     (color-hex (lookup-hex name))))


(define (color-name? name)
  (> (lookup-hex name) -1))
