mkdir distrib
xcopy racket distrib\racket /i
xcopy models distrib\models /i
xcopy shaders distrib\shaders /i
copy tangerine.exe distrib\tangerine.exe
copy LICENSE.txt distrib\LICENSE.txt
copy SDL2.dll distrib\SDL2.dll
copy libracketcs_d9hn5s.dll distrib\libracketcs_d9hn5s.dll
