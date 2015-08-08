/*------------------------------------------------------------------------------
 * UrlEncodeTest.c -- Test for URL encoding and decoding
 *
 * Copyright (c) 2011 Kevin Short.
 */
#include <stdio.h>
// #include <stdlib.h>
// #include <string.h>

#include "UrlEncode.h"
#include "dbug.h"

/*
 * stanadlone test program.
 */
int main(int argc, char** argv)
{
    static char* s =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"01234567890"
	" "
	"!*'();:@&=+$,/?#[]"
	;

    char* encoded;
    char* decoded;

    DBUG_PUSH("d:t");
    DBUG_ENTER("main");

    encoded = UrlEncode(s);
    decoded = UrlDecode(encoded);

    DBUG_PRINT("test",(s));
    DBUG_PRINT("test",(encoded));
    DBUG_PRINT("test",(decoded));
    DBUG_PRINT("test",((0 == strcmp(s, decoded)) ? "Looks good!" : "Failed"));

    free(encoded);
    free(decoded);

    DBUG_RETURN(0);
}

/*
 * EOF
 */
