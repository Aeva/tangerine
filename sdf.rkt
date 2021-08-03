#lang at-exp racket/base
(require racket/format)

(provide scene
         sphere
         union
         inter
         cut
         smooth-union
         smooth-inter
         smooth-cut
         trans)

(struct sdf-part (wrapped))

(define (scene sdf-body)
  (~a "float SceneDist(vec3 Point)\n{\n\treturn "
      ((sdf-part-wrapped sdf-body) "Point")
      ";\n}\n"))

(define (sphere diameter)
  (sdf-part
   (λ (point)
    @~a{SphereBrush(@point, @diameter)})))


(define (make-operator op)
  (λ (lhs rhs)
    (sdf-part
     (λ (point)
       (let ([lhs-branch ((sdf-part-wrapped lhs) point)]
             [rhs-branch ((sdf-part-wrapped rhs) point)])
         @~a{@op(@lhs-branch, @rhs-branch)})))))


(define union (make-operator "UnionOp"))
(define inter (make-operator "IntersectionOp"))
(define cut (make-operator "CutOp"))


(define (make-smooth op)
  (λ (span lhs rhs)
    (sdf-part
     (λ (point)
       (let ([lhs-branch ((sdf-part-wrapped lhs) point)]
             [rhs-branch ((sdf-part-wrapped rhs) point)])
         @~a{@op(@lhs-branch, @rhs-branch, @span)})))))


(define smooth-union (make-smooth "SmoothUnionOp"))
(define smooth-inter (make-smooth "SmoothIntersectionOp"))
(define smooth-cut (make-smooth "SmoothCutOp"))


(define (trans x y z child)
  (sdf-part
   (λ (point)
     ((sdf-part-wrapped child) @~a{(@point - vec3(@x, @y, @z))}))))

