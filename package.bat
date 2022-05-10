mkdir distrib
xcopy racket distrib\racket /i
xcopy racket\etc distrib\racket\etc /i
xcopy racket\collects distrib\racket\collects /i /S
xcopy models distrib\models /i
xcopy shaders distrib\shaders /i
xcopy materials distrib\materials /i
copy tangerine.exe distrib\tangerine.exe
copy LICENSE.txt distrib\LICENSE.txt
copy SDL2.dll distrib\SDL2.dll
copy libracketcs_db9xz4.dll distrib\libracketcs_db9xz4.dll
