#lang at-exp racket/base

(require racket/list)
(require racket/string)
(require racket/format)
(require "csgst.rkt")
(require "bounds-compiler.rkt")
(require "vec.rkt")


(provide compile-bpy)


(define (tuple vec)
  (let-values ([(x y z) (apply values vec)])
    @~a{(@x, @y, @z)}))


(define (compile-bpy csgst)
  (string-join
   (flatten
    (list
     "import bpy, bmesh"
     (for/list ([part (in-list (segments csgst))])
       (let ([subtree (car part)]
             [bounds (cdr part)])
         (for/list ([bound (in-list bounds)])
           (let* ([low (car bound)]
                  [high (cdr bound)]
                  [extent (vec- high low)]
                  [center (tuple (vec+ (vec* extent .5) low))]
                  [scale (tuple extent)])
             (list
              @~a{bpy.ops.mesh.primitive_cube_add(size=1.0, location=@center)}
              "obj = bpy.context.active_object"
              "obj.select_set(True)"
              "bpy.ops.object.mode_set(mode = 'EDIT')"
              "mesh = bmesh.from_edit_mesh(obj.data)"
              @~a{bmesh.ops.scale(mesh, vec=@scale, verts=mesh.verts)}
              "bmesh.update_edit_mesh(obj.data)"
              "bpy.ops.object.mode_set(mode = 'OBJECT')")))))))
   "\n"))


;(display
; (compile-bpy (union
;               (sphere 2)
;               (move 1 1 1 (sphere 2)))))
