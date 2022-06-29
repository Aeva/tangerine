# Writing Tangerine Models with Lua

A typical Tangerine model written in Lua looks like this:

```lua
model =
	inter(cube(4), sphere(5.5))
	:diff(
		cylinder(3, 5),
		cylinder(3, 5):rotate_x(90),
		cylinder(3, 5):rotate_y(90))
```

The above model can also be rewritten like this:

```lua
model =
	diff(
		inter(
			cube(4),
			sphere(5.5)),
		cylinder(3, 5),
		rotate_x(cylinder(3, 5), 90),
		rotate_y(cylinder(3, 5), 90))
```

Personally, I find the first variant to be more useful, but both styles are valid.

The important thing is that your program constructs a valid CSG tree and stores it in a variable named `model`.

# CSG Tree Construction APIs

The entire Tangerine API is automatically loaded into the global environment.  However, should you desire to access these through a common table, they can also be found under the global variable `tangerine`.

All of these functions return a valid CSG node, and may be chained together using the `:` operator.

## Brush Functions

Brush functions draw shapes.

 * `sphere(diameter)`

 * `ellipsoid(diameter_x, diameter_y, diameter_z)`

 * `box(width, length, height)`

 * `cube(widthlengthheight)`

 * `torus(major_diameter, minor_diameter)`

 * `cylinder(diameter, height)`

## The Infinite Plane Function

This function can be used like the brush functions above, but it results in a shape with an infinite bounding box.

Tangerine is not able to compile models with an infinite bounding volume.  This limitation restricts where it is valid to use the `plane` function.

This function can always be safely used as a right-hand operand to a `diff` operator.

This function can be safely combined with other shapes using all available operators, provided that result is eventually combined with a finite shape via the `inter` operator.

 * `plane(normal_x, normal_y, normal_z)`

## Basic Operators

Operators are used to construct complex models.  These accept at least two CSG nodes as arguments, but you may provide more.

 * `union(lhs, rhs, ...)`

 * `diff(lhs, rhs, ...)`

 * `inter(lhs, rhs, ...)`

## Blending Operators

Blending operators provide a smooth transition between their operands.  The degree of smoothness is determined by the threshold parameter.  These accept at least two CSG nodes as arguments, but you may provide more.  The last argument must be the threshold value.

 * `blend_union(lhs, rhs, ..., threshold)`

 * `blend_diff(lhs, rhs, ..., threshold)`

 * `blend_inter(lhs, rhs, ..., threshold)`

## Transform Modifiers

These may be applied to brushes and operators.

 * `move(node, x, y, z)`

 * `move_x(node, x)`

 * `move_y(node, y)`

 * `move_z(node, z)`

 * `rotate_x(node, degrees)`

 * `rotate_y(node, degrees)`

 * `rotate_z(node, degrees)`

## The Align Modifier

The `align` function offsets the node graph you pass into it to reposition it relative to its local bounding box.  In other words, this function is used to determine where a node graph's local origin should be.

 * `align(node, alignment_x, alignment_y, alignment_z)`

The alignment parameters are specified per-axis, and must be between `-1.0` and `1.0`.  The `align` modifier uses the inner bounding box of the expression tree, which means it ignores the padding that would otherwise be introduced by blend operators.

For example `align(cube(1):move_x(10), 0, 0, 0)` creates a cube centered at the origin, negating the `move_x`.

For another example, `align(cube(1), -1, -1, -1)` creates a cube who's negative-most corner will be the origin.

## The Paint Modifier

The `paint` modifier assigns a color to all shapes in the given subtree.  This will not affect any shapes that have already been painted.

The `paint_over` variant works the same as `paint`, but will override existing colors in the given subtree.

 * `paint(node, color_string)`

 * `paint_over(node, color_string)`

Both functions accept three forms: three and six digit HTML color codes, and CSS color names.  All Standard CSS color names are supported.

The following examples of the `paint` modifier are equivalent:

 * `paint(sphere(1), "#xF00")`

 * `paint(sphere(1), "#xFF0000")`

 * `paint(sphere(1), "red")`

# Miscellaneous APIs
## Background Manipulation

This function will force the renderer to switch to a solid color background, and will set the background color to the provided color.  The color string should be either a three or six digit HTML color code, or a CSS color name.  All Standard CSS color names are supported.

 * `set_bg(color_string)` returns `nil`.
