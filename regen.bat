cd ..
lib /def:third_party\racket\lib\libracketcs_d9hn5s.def /out:third_party\racket\lib\x64\libracketcs_d9hn5s.lib /machine:x64
raco ctool --mods racket\modules ++lib tangerine ++lib racket/base/lang/reader ++lib racket/runtime-config --runtime racket
