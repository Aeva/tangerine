raco exe -v -o delete_me --collects-dest racket/collects ++lib tangerine ++lib tangerine/smol ++lib vec ++lib racket/base/lang/reader ++lib s-exp/lang/reader ++lib at-exp/lang/reader ++lib racket/runtime-config ++lib ffi/unsafe models/basic_thing.rkt

rm delete_me
