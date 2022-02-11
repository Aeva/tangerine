mkdir distrib
xcopy racket distrib\racket /i
xcopy models distrib\models /i
xcopy shaders distrib\shaders /i
xcopy materials distrib\materials /i
copy tangerine.exe distrib\tangerine.exe
copy LICENSE.txt distrib\LICENSE.txt
copy SDL2.dll distrib\SDL2.dll
copy libracketcs_da32rk.dll distrib\libracketcs_da32rk.dll
