# What is this?

Tangerine is a system for constructing, manipulating, evaluating, manifesting, and sometimes rendering
signed distance functions.  There are two versions:

 * Tangerine "*Miniscule*" is a Racket library with no renderer, and can be used to generate [MagicaVoxel](https://ephtracy.github.io/) files.

 * Tangerine "*Majuscule*" is a standalone application, and you are strongly discouraged from using it at this time.

# Installing Tangerine "*Miniscule*" (x64 Windows and Linux)

 0. If using Windows, install the [latest x64 Microsoft Visual C++ Redistributable for Visual Studio 2019](https://aka.ms/vs/17/release/vc_redist.x64.exe).
    For some remarkable reason this is not installed by default.

 1. Install [Racket CS 8.3](https://download.racket-lang.org/all-versions.html),
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

# Development Environment Setup

## Windows

This will build both the "*Miniscule*" and "*Majuscule*" versions of Tangerine.

 1. Install Racket CS 8.3, and ensure that `raco` is in your system path variable.

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

 1. Install Racket CS 8.3, and ensure that `raco` is in your system path variable.

 2. Install Clang 13 or so.

 3. Clone this project somewhere.

 4. Open the command line, cd into the project folder, and then run
    `raco pkg install --link package/tangerine` to install the Tangerine Racket module from this repository.
    If you already have another version, uninstall it first.

 5. Run `build.sh` to regenerate `package/tangerine/tangerine.so`.
