cd ..

lib /def:third_party\racket\lib\libracketcs_db9xz4.def /out:third_party\racket\lib\x64\libracketcs_db9xz4.lib /machine:x64

raco exe -v -o .\delete_me.exe --collects-dest racket\collects ++lib tangerine ++lib tangerine/smol ++lib racket/base/lang/reader ++lib s-exp/lang/reader ++lib at-exp/lang/reader ++lib racket/runtime-config ++lib ffi/unsafe models\basic_thing.rkt

del delete_me.exe
