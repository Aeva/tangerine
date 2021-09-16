mkdir distrib
xcopy csg distrib\csg /i
xcopy models distrib\models /i
xcopy shaders distrib\shaders /i
copy csg.rkt distrib\csg.rkt
copy LICENSE.txt distrib\LICENSE.txt
raco exe ++lang "at-exp racket/base" ++lib "racket/list" ++lib "racket/string" ++lib "racket/format" ++lib "math/flonum" ++lib "racket/math" tangerine.rkt
raco dist .\distrib\ tangerine.exe
