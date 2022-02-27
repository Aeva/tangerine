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

(require racket/list)
(require ffi/unsafe)
(require "ffi.rkt")
(require "eval.rkt")
(require "coalesce.rkt")

(provide export-magica)

(define-backend ExportMagicaVoxel (_fun _HANDLE _float _int _string/utf-8 -> _void))

(define (export-magica csgst grid-size pallet-index path)
  (let* ([folded (coalesce csgst)]
         [model (sdf-build folded)])
    (unless (sdf-handle-is-valid? model)
      (error "Can't find backend dll?"))
    (display "Exporting...")
    (ExportMagicaVoxel (cdr model) grid-size pallet-index path)
    (display " Magikazam!\n")))
 