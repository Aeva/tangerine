cd ..
lib /def:third_party\racket\lib\libracketcs_d9hn5s.def /out:third_party\racket\lib\x64\libracketcs_d9hn5s.lib /machine:x64
raco ctool --c-mods third_party\racket\include\generated.hpp ++lib racket/base --runtime racket
