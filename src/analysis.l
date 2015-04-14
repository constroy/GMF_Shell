%{
	#include "bison.tab.h"
%}

%%
"<"	{return '<';}
">"	{return '>';}
"&"	{return '&';}
"|"	{return '|';}
[^<>&\n \t|]+ {return STRING;}
%%
