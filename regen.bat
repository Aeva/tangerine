cd ..
lib /def:third_party\racket\lib\libracketcs_da32rk.def /out:third_party\racket\lib\x64\libracketcs_da32rk.lib /machine:x64
raco ctool --mods racket\modules ++lib tangerine ++lib racket/base/lang/reader ++lib at-exp/lang/reader ++lib racket/runtime-config ++lib ffi/unsafe --runtime racket
