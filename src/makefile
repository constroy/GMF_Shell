user-sh : bison.tab.o execute.o
	cc -o user-sh bison.tab.o lex.yy.o execute.o -lfl
bison.tab.o : bison global.h
	cc -c bison.tab.c lex.yy.c
execute.o : execute.c global.h
	cc -c execute.c
bison :	analysis.l bison.y
	bison -d bison.y
	flex analysis.l
	
clean :
	rm user-sh bison.tab.o execute.o bison.tab.c
