# What is this?

Tangerine is a system for constructing, manipulating, evaluating, manifesting, and sometimes rendering
signed distance functions.  There are two versions:

 * Tangerine "*Miniscule*" is a Racket library with no renderer, and can be used to generate [MagicaVoxel](https://ephtracy.github.io/) files and binary STL files.

 * Tangerine "*Majuscule*" is a standalone application, and you are strongly discouraged from using it at this time.

# Installing Tangerine "*Miniscule*" (x64 Windows and Linux)

 1. Install [Racket CS 8.5](https://download.racket-lang.org/all-versions.html),
    and ensure that `raco` and `racket` are in your system path environment variable.  Newer versions might be fine.

 2. Open the command line, and run `raco pkg install tangerine`.

*If the install fails with this error, try reinstalling Racket from a different mirror:*
*`ssl-connect: connect failed (error:1416F086:SSL routines:tls_process_server_certificate:certificate verify failed)`*

# Installing Tangerine "*Majuscule*"

This space intentionally left blank.

# Writing Your First Tangerine Model

 1. Copy this source into a new file called `my-first-tangerine.rkt`:
    ```racket
    #lang racket/base

    (require tangerine)
    (require tangerine/export)

    (let ([model (diff
                  (inter
                   (cube 4)
                   (sphere 5.5))
                  (cylinder 3 5)
                  (rotate-x 90 (cylinder 3 5))
                  (rotate-y 90 (cylinder 3 5)))]
          [voxels-per-unit 100.0]
          [color-index 24]
          [path "my-first-tangerine.vox"])
      (export-magica model voxels-per-unit color-index path))
    ```

 2. Open the command line, cd to the directory where that file is saved, and then run `racket my-first-tangerine.rkt`.
    This may take a few seconds to several minutes to run depending on your CPU and the size of your model, and it is normal for your fans to spin up aggressively.
    This will create `my-first-tangerine.vox` in the same folder.

 3. Open `my-first-tangerine.vox` in [MagicaVoxel](https://ephtracy.github.io/) to see what your model looks like!

**SPOILERS**

It'll look like this:

![A MagicaVoxel rendering of a hollowed out cube with rounded corners.](https://raw.githubusercontent.com/Aeva/tangerine/excelsior/spoilers.png "Magikazam!")

# Writing Your *Second* Tangerine Model

Here's all of the functions you can use to construct models with in Tangerine:

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

# Writing Your *Third* Tangerine Model

There's more examples in the `models` folder of this repository for you to take inspiration from.
They're written for Tangerine "*Majuscule*", but they can be made to work with Tangerine "*Miniscule*" by
simply ignoring all of the stuff I have not explained yet.

# Development Environment Setup

## Windows

This will build both the "*Miniscule*" and "*Majuscule*" versions of Tangerine.

 1. Install Racket CS 8.5, and ensure that `raco` is in your system path variable.

 2. Install Visual Studio 2019 or newer with at least the C++ stuff.

 3. Clone this project somewhere.

 4. Open the command line, cd into the project folder, and then run
    `raco pkg install --link package/tangerine` to install the Tangerine Racket module from this repository.
    If you already have another version, uninstall it first.

 5. Open windows/tangerine.sln, and rebuild all projects in either the Debug or Release config.
    You should now be able to run the program from the debugger.  The project `tangerine` is the standalone
    application, and the project `headless` provides the dll for the racket library.

 6. (Optional) Run `package.bat` to package Tangerine "*Majuscule*" up into a distributable form in the folder `distrib`.

## Linux

This will only build the "*Miniscule*" version of Tangerine.

 1. Install Racket CS 8.5, and ensure that `raco` is in your system path variable.

 2. Install Clang 13 or so.

 3. Clone this project somewhere.

 4. Open the command line, cd into the project folder, and then run
    `raco pkg install --link package/tangerine` to install the Tangerine Racket module from this repository.
    If you already have another version, uninstall it first.

 5. Run `build.sh` to regenerate `package/tangerine/tangerine.so`.
