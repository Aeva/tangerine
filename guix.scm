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
#;
(define-public racket-vec-lib
  (let ((commit "c3f29bc928d5900971f65965feaae59e1272a3f7")
        (revision "1")) ; Guix package revision
    (package
      (name "racket-vec-lib")
      (version (git-version "0.0" revision commit))
      (source (origin
                (method git-fetch)
                (uri (git-reference
                      (url "https://github.com/Aeva/vec")
                      (commit commit)))
                (sha256
                 (base32
                  "0l515l9kx39xqa9skj7pj54gwffypa1d9pri7daj7kdzqs6d8cc8"))
                (file-name (git-file-name name version))))
      (build-system racket-build-system)
      (inputs
       (list racket-base))
      (arguments
       (list #:path "vec-lib"))
      (home-page "https://pkgs.racket-lang.org/package/vec-lib")
      (synopsis "Simple vector math library (implementation part)")
      (description
       "This package provides a simple Racket library for vector math,
developed for Tangerine.  For documentation, see the Racket package
@code{vec}.")
      (license license:asl2.0))))
#;
(define-public racket-tangerine-x86-64-linux
  (package
    (name "racket-tangerine-x86-64-linux")
    (version "0.0")
    (origin %tangerine-origin)
    (build-system racket-build-system)
    (inputs
     (list racket-base))
    (arguments
     (list
      #:path "package/tangerine-x86_64-linux"
      #:phases
      #~(modify-phases %standard-phases
          (add-after 'unpack 'patch-info-rkt
            (lambda args
              (substitute* "info.rkt"
                (("[(]define copy-foreign-libs")
                 "#;(define copy-foreign-libs")))))))
    (home-page "https://pkgs.racket-lang.org/package/tangerine-x86-64-linux")
    (synopsis "")
    (description "")
    (license license:asl2)))
#;
(define-public racket-tangerine
  (package
    (name "racket-tangerine")
    (version "0.0")
    (origin %tangerine-origin)
    (build-system racket-build-system)
    (inputs
     (list racket-base
           racket-sandbox-lib
           racket-tangerine-x86-64-linux
           racket-vec-lib))
    (arguments
     (list #:path "package/tangerine"))
    (home-page "https://pkgs.racket-lang.org/package/tangerine-x86-64-linux")
    (synopsis "")
    (description "")
    (license license:asl2)))

tangerine
