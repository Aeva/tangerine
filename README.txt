Hello curious person!

This is a branch for experiments orthogonal to Tangerine's main codebase.
The purpose of this branch is simple:  Tangerine's internals are getting to be
fairly significant in implementation, there are several major features that I
want to build that I don't have a clear vision for a best implementation, and
both of these things together is a recipe for repeated lengthy refactoring.
As it stands right now, only have a few hours a week or every few weeks that I
can realistically work on this project, so I need to take a novel approach to
this problem, otherwise I just can't work on Tangerine for the forseeable
future.

My solution for now is to prototype new features and architectural ideas in C#.
C# is attractive for this, because the language is similar to C++, but is softer
in several key areas that make it more suitable for rapid iteration.

My hope is that I will be able to decide upon a set of architectural decisions
and algorithms that will be suitable to translate into equivalent C++.  I'd
like the result of this exercise to be a C++ kernel that can be ported to many
things, as well as a C# wrapper for it to make it easy to use with MonoGame and
FNA.

This branch is named "chemical_lemonade" for a mini theme camp I came across at
Lakes of Fire years ago, where the lovely people there were giving out lemonade
made from a precisely calibrated mixture of water, citric acid powder, and sugar
which they named "chemical lemonade".  It was delicious.
