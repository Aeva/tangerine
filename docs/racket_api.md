# Writing Tangerine Models with Racket

A typical Tangerine model written in Racket looks like this:

```racket
#lang racket/base

(require tangerine)
(provide model)

(define model (diff (inter (cube 4) (sphere 5.5))
                    (cylinder 3 5)
                    (rotate-x 90 (cylinder 3 5))
                    (rotate-y 90 (cylinder 3 5))))
```

# Tangerine Racket API Reference

## Brush Functions

Brush functions draw shapes.

 * `(sphere diameter)`

 * `(ellipsoid diameter-x diameter-y diameter-z)`

 * `(box width length height)`

 * `(cube widthlengthheight)`

 * `(torus major-diameter minor-diameter)`

 * `(cylinder diameter height)`

## The Infinite Plane Function

This function can be used like the brush functions above, but it results in a shape with an infinite bounding box.

Tangerine is not able to compile models with an infinite bounding volume.  This limitation restricts where it is valid to use the `plane` function.

This function can always be safely used as a right-hand operand to a `diff` operator.

This function can be safely combined with other shapes using all available operators, provided that result is eventually combined with a finite shape via the `inter` operator.

 * `(plane normal-x normal-y normal-z)`

## Basic Operators

Operators are used to construct complex models.

 * `(union lhs rhs ...)`

 * `(diff lhs rhs ...)`

 * `(inter lhs rhs ...)`

## Blending Operators

Blending operators provide a smooth transition between their operands.
The degree of smoothness is determined by the threshold parameter.

 * `(blend union threshold lhs rhs ...)`

 * `(blend diff threshold lhs rhs ...)`

 * `(blend inter threshold lhs rhs ...)`

## Transform Modifiers

These may be applied to brushes and operators.

 * `(move offset-x offset-y offset-z csgst)`

 * `(move-x offset-x csgst)`

 * `(move-y offset-y csgst)`

 * `(move-z offset-z csgst)`

 * `(rotate-x degrees csgst)`

 * `(rotate-y degrees csgst)`

 * `(rotate-z degrees csgst)`

## The Align Modifier

The `align` modifier offsets the expression tree to align it with the origin.
The alignment parameters are specified per-axis, and must be between `-1.0` and `1.0`.
The `align` modifier uses the inner bounding box of the expression tree, which
means it ignores the padding that would otherwise be introduced by blend operators.

For example `(align 0 0 0 (move-x 10 (cube 1)))` creates a cube centered at the origin, negating the move-x.

For another example, `(align -1 -1 -1 (cube 1))` creates a cube who's negative-most corner is on the origin.

 * `(align origin-x origin-y origin-z csgst)`

## The Paint Annotation

The `paint` annotation assigns a color to all shapes in the given subtree.
If the shape already has a color applied, then the old color will be used.

The `paint-over` variant works the same as `paint`, but will override existing colors in the given subtree.

Both versions accept three forms: six digit HTML color codes, CSS color names, or three numbers.
The following examples of the `paint` annotation are equivalent:

 * `(paint #xFF0000 (sphere 1))`

 * `(paint 'red (sphere 1))`

 * `(paint 1. 0. 0. (sphere 1))`

## Export Functions

 * `(export-magica csgst grid-size pallet-index path)`

 * `(export-ply csgst grid-size path [refinement-iterations 5])`

 * `(export-stl csgst grid-size path [refinement-iterations 5])`
