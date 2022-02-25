# What is this?

Don't worry about it.

## Building on Windows

 1. Install Racket CS 8.3, and ensure that `raco` is in your system path variable.

 2. Install Visual Studio 2019 or newer with at least the C++ stuff.

 3. Clone this project somewhere.

 4. Open the command line, cd into the "package" directory in the project
    folder, and then run `raco pkg install --link tangerine` to install tangerine's racket module.

 5. Open windows/tangerine.sln, and rebuild all projects in either the Debug or Release config.
    You should now be able to run the program from the debugger.  The project `tangerine` is the standalone
    application, and the project `headless` provides the dll for the racket library.

 6. Run `package.bat` to package tangerine up into a minimal form in the folder "distrib".

## Building on Linux

TODO
