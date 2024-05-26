# Contributing to Tangerine

Contributions are welcome!
Here are some pointers.

## C++ Style Guide

C++ code written for Tangerine (i.e. in `tangerine/`, not in `third_party/`) should generally follow the [Unreal Engine coding standards](https://docs.unrealengine.com/5.1/en-US/epic-cplusplus-coding-standard-for-unreal-engine/), except that Tangerine does not use Hungarian notation.

The main points are:

  - Indent with tabs, one tab per indentation level.
    Tabs are to be 4 characters wide.

  - Braces are always on their own line at the same indentation level as the surrounding scope.

  - `TitleCase` is used for all class names, function names, variable names, constants, and so on.

  - Variable names should be descriptive and useful.

  - Single digit variables may be lower case for loop counters, but should generally be uppercase otherwise.

  - Single digit variable names should be avoided in favor of more descriptive names, but they do show up as loop counters, coordinates, and sometimes math terms.

  - `UPPER_SNAKE_CASE` is used for preprocessor defines.

  - Preprocessor directives should be lowercase and have no space between the `#` and the name, like so: `#define FNORD 1`.

  - Preprocessor directives generally aren't indented, but may be if it improves clarity.

  - `snake_case` is used for labels.

  - Labels should not be indented.

  - `goto` is acceptable when it improves readability.

  - There's no line limit, but the code should probably read ok on a modest 1080p monitor in a full screen window with a reasonable font size.
