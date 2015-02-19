main: main.c good.h
	cc $@.c -o $@

good.h: buildresponse country-state.csv 
	./buildresponse country-state.csv >$@
