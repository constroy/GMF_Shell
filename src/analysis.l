%{
	#include "bison.tab.h"
 	#include "global.h"
%}

%%
"<"	{return "<";}
">"	{return ">";}
"&"	{return "&";}
"\n"	{return EOL;}
[ \T]  	{}
[^<>&\n \t]+ {strcpy(inputBuff,yytext); return STRING;}
%%
