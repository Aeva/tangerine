# What is this?

Don't worry about it.

## Building on Windows

 1. Install Racket CS 8.2 or possibly newer, and ensure that `raco` is in your system path variable.

 2. Install Visual Studio 2019 or newer with at least the C++ stuff.

 3. Clone this project somewhere.

 4. Open the command line, cd into the project directory, and run `raco pkg install --link tangerine`
    to install tangerine's racket module.

 5. Open windows/tangerine.sln, and rebuild the `tangerine` project in either the Debug or Release config.
    You should now be able to run the program from the debugger.

 6. Run `package.bat` to package tangerine up into a minimal form in the folder "distrib".

## Building on Linux

TODO
