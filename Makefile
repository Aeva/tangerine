ZUO = racket/racket/src/build/bin/zuo

.PHONY: all
all: $(ZUO)
	$(ZUO) .

racket/racket/src/build/bin/zuo:
	cd racket && $(MAKE) racket/src/build/bin/zuo

racket\racket\src\build\zuo.exe:
	cd racket && $(MAKE) racket\src\build\zuo.exe
