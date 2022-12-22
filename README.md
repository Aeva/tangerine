# What is this?

Tangerine is a system for creating 3D models procedurally from a set of Signed Distance Function (SDF) primitive shapes and combining operators.
Models are written in Lua, and may be either rendered directly or exported to a variety of common 3D model file formats.

Tangerine currently supports Windows and Linux:

 * Windows release builds can be downloaded from [itch.io](https://aeva.itch.io/tangerine).

 * The Linux version must be built from source.
   Build instructions are at the end of this readme.

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

 1. Install cmake, clang, and SDL2 by whatever means are appropriate for your distro.
    The other dependencies are provided by this repository.
    As several of the included dependencies contain modifications for this project, it is not advisable to replace them with packaged dependencies.
    I apologize for the convenience.

 2. Clone this project somewhere.

 3. Run `linux/build_majuscule.sh`.

 4. If all goes well, the binary will show up in `linux/build/Release/tangerine`.
    Rename it to something like `a.out` and copy it into the root project folder (it must be in the same directory as the `shaders` folder to work).
