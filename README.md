# What is this?

Tangerine is a system for creating 3D models procedurally from a set of Signed Distance Function (SDF) primitive shapes and combining operators.
Models are written in Lua, and may be either rendered directly or exported to a variety of common 3D model file formats.

Tangerine currently supports Windows and Linux:

 * Windows release builds can be downloaded from [itch.io](https://aeva.itch.io/tangerine).

 * The Linux version must be built from source.
   Build instructions are at the end of this readme.
   Optionally, you can use Guix to automate the build process.

# Getting Started

When you first run Tangerine, you will be greeted with a screen of static.
Open one of the models in the `models` directory to see a working example.
These are provided as a programming reference, and for regression testing.
There is also [API reference documentation](docs/lua_api.md) available.

# Development Environment Setup

## Windows

 0. Install Racket CS 8.5, and ensure that `raco` is in your system path variable.  This requirement will be removed in the future.

 1. Install Visual Studio 2022 or newer with at least the C++ stuff.

 2. Clone this project somewhere.

 3. Open `windows/tangerine.sln`, and then rebuild the project `tangerine` in either the Debug or Release config.
    You should now be able to run Tangerine from visual studio for debugging or create your own release builds.

 4. (Optional) Run `package.bat` to package Tangerine up into a distributable form in the folder `distrib`.

## Linux

You can build the project directly, or you can use [Guix](https://guix.gnu.org) to manage the build environment.

### Building Manually

 1. Install CMake, a C++17 toolchain (Clang and GCC both seem to work), and SDL2 by whatever means are appropriate for your distro.
    (If you encounter errors about linking to `tinfo`, the problem may be related to Curses.)
    The other dependencies are provided by this repository.
    As several of the included dependencies contain modifications for this project, it is not advisable to replace them with packaged dependencies.
    I apologize for the convenience.

 2. Clone this project somewhere.

 3. Run `./linux/build_majuscule.sh`.

 4. If all goes well, the binary will show up in `./linux/build/Release/bin/tangerine`: you can run it directly from the build directory.
    (If you want to move it somewhere else, beware that it must find some runtime data from a path relative to the executable: `cmake --install` handles keeping them together.)

Of course, `./linux/build_majuscule.sh` just runs `cmake` with some sensible options.
If you prefer, you can always run `cmake` directly.

### Building with Guix

If you install Guix (which may be as easy as `sudo apt install guix`), you don't have to worry about any of the other dependencies.

To run Tangerine, automatically building it if needed, try:

```sh
guix time-machine -C channels.scm -- \
  shell --rebuild-cache -f guix.scm -- \
    tangerine
```

You can create an isolated container and enter a shell for interactive building by running:

```sh
guix time-machine -C channels.scm -- \
  shell --rebuild-cache -D -f guix.scm --container --emulate-fhs
```

Adding `-- ./linux/build_majuscule.sh` to the end of that command will perform a build in the same way as the manual process described above.

# Miscellaneous

## FreeType License Disclosure

Tangerine optionally integrates RmlUi, which in turn typically requires linking with FreeType.
Per the FreeType project license, the use of FreeType must be acknowldeged somewhere in a project's documentation.
The use of FreeType in Tangerine via an optional dependency is hereby acknowledged in Tangerine's documentation.
