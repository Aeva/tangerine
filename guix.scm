;; A Guix package to build Tangerine from a local checkout

(use-modules
 (gnu packages cmake)
 (gnu packages ncurses)
 (gnu packages sdl)
 (guix build-system cmake)
 (guix gexp)
 (guix git-download)
 ((guix licenses) #:prefix license:)
 (guix packages)
 (guix utils)
 (ice-9 regex))

(define-public %tangerine-origin
  (local-file
   "." "tangerine-src"
   #:recursive? #t
   #:select?
   (let* ((src-dir (current-source-directory))
          (checked-in?
           (or (and src-dir
                    (git-predicate (canonicalize-path src-dir)))
               (lambda (path stat)
                 #t)))
          (so-dll-rx
           (make-regexp "\\.(so|dll)$"))
          (so-or-dll?
           (lambda (path stat)
             (regexp-exec so-dll-rx path)))
          (source-file?
           (lambda (path stat)
             (and (checked-in? path stat)
                  (not (so-or-dll? path stat))))))
     source-file?)))

(define-public tangerine
  (package
    (name "tangerine")
    (version "0.0")
    (source %tangerine-origin)
    (outputs '("out" "debug"))
    (build-system cmake-build-system)
    (inputs
     (list ncurses/tinfo ; for -ltinfo
           sdl2
           ;; are the rest needed?
           sdl2-image
           sdl2-mixer
           sdl2-ttf))
    (arguments
     (list
      #:cmake cmake ; newer than cmake-minimal
      #:tests? #f))
    (home-page "https://github.com/Aeva/tangerine")
    (synopsis "Procedural 3D model creation")
    (description
     "Tangerine is a system for creating 3D models procedurally from a set
of Signed Distance Function (SDF) primitive shapes and combining
operators.  Models are written in Lua, and may be either rendered
directly or exported to a variety of common 3D model file formats.")
    (license license:asl2.0)))

tangerine
