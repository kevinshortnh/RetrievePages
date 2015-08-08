/*------------------------------------------------------------------------------
 * UrlEncode.c -- URL encoding and decoding
 *
 * Reserved characters:
 *
 *  ! * ' ( ) ; : @ & = + $ , / ? # [ ]
 *
 * Copyright (c) 2011 Kevin Short.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "UrlEncode.h"
#include "dbug.h"

#define Hex2Bin(ch) (isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10)

/*------------------------------------------------------------------------------
 * Use a lookup table for encodings.
 *
 * This ought to be faster than performing various tests based on character
 * class.
 */
static char* urlEncoding[256] =
{
    "%00", "%01", "%02", "%03", "%04", "%05", "%06", "%07",
    "%08", "%09", "%0A", "%0B", "%0C", "%0D", "%0E", "%0F",
    "%10", "%11", "%12", "%13", "%14", "%15", "%16", "%17",
    "%18", "%19", "%1A", "%1B", "%1C", "%1D", "%1E", "%1F",
      "+", "%21", "%22", "%23", "%24", "%25", "%26", "%27",
    "%28", "%29", "%2A", "%2B", "%2C",   "-",   ".", "%2F",
      "0",   "1",   "2",   "3",   "4",   "5",   "6",   "7",
      "8",   "9", "%3A", "%3B", "%3C", "%3D", "%3E", "%3F",
    "%40",   "A",   "B",   "C",   "D",   "E",   "F",   "G",
      "H",   "I",   "J",   "K",   "L",   "M",   "N",   "O",
      "P",   "Q",   "R",   "S",   "T",   "U",   "V",   "W",
      "X",   "Y",   "Z", "%5B", "%5C", "%5D", "%5E",   "_",
    "%60",   "a",   "b",   "c",   "d",   "e",   "f",   "g",
      "h",   "i",   "j",   "k",   "l",   "m",   "n",   "o",
      "p",   "q",   "r",   "s",   "t",   "u",   "v",   "w",
      "x",   "y",   "z", "%7B", "%7C", "%7D",   "~", "%7F",
    "%80", "%81", "%82", "%83", "%84", "%85", "%86", "%87",
    "%88", "%89", "%8A", "%8B", "%8C", "%8D", "%8E", "%8F",
    "%90", "%91", "%92", "%93", "%94", "%95", "%96", "%97",
    "%98", "%99", "%9A", "%9B", "%9C", "%9D", "%9E", "%9F",
    "%A0", "%A1", "%A2", "%A3", "%A4", "%A5", "%A6", "%A7",
    "%A8", "%A9", "%AA", "%AB", "%AC", "%AD", "%AE", "%AF",
    "%B0", "%B1", "%B2", "%B3", "%B4", "%B5", "%B6", "%B7",
    "%B8", "%B9", "%BA", "%BB", "%BC", "%BD", "%BE", "%BF",
    "%C0", "%C1", "%C2", "%C3", "%C4", "%C5", "%C6", "%C7",
    "%C8", "%C9", "%CA", "%CB", "%CC", "%CD", "%CE", "%CF",
    "%D0", "%D1", "%D2", "%D3", "%D4", "%D5", "%D6", "%D7",
    "%D8", "%D9", "%DA", "%DB", "%DC", "%DD", "%DE", "%DF",
    "%E0", "%E1", "%E2", "%E3", "%E4", "%E5", "%E6", "%E7",
    "%E8", "%E9", "%EA", "%EB", "%EC", "%ED", "%EE", "%EF",
    "%F0", "%F1", "%F2", "%F3", "%F4", "%F5", "%F6", "%F7",
    "%F8", "%F9", "%FA", "%FB", "%FC", "%FD", "%FE", "%FF",
};

/*------------------------------------------------------------------------------
 * Encode a URL string.
 *
 * Returns the encoded string -- which the caller must 'free()'.
 *
 * Returns NULL if passed a NULL pointer.
 */
char* UrlEncode(char* string)
{
    char* result;
    unsigned char* s;
    char* r;

    DBUG_ENTER("UrlEncode");

    if (NULL == string)
    {
	DBUG_RETURN(NULL);
    }

    /* allocate enough space for complete encoding */
    result = malloc(3 * strlen(string) + 1);

    for (s = (unsigned char*)string, r = result; *s; ++s)
    {
	char* p = urlEncoding[*s];	/* lookup */
	while (*p)
	{
	    *r++ = *p++;		/* copy string */
	}
    }

    *r = '\0';				/* terminate string */

    DBUG_RETURN(result);
}

/*
 * Decode a URL string.
 *
 * Returns the decoded string -- which the caller must 'free()'.
 *
 * Returns NULL if passed a NULL pointer.
 * Returns NULL if an invalid "%xx" sequence is encountered.
 */
char* UrlDecode(char* string)
{
    char* result;
    char* s;
    char* r;

    DBUG_ENTER("UrlDecode");

    if (NULL == string)
    {
	DBUG_RETURN(NULL);
    }

    /* allocate enough space for straight pass through */
    result = malloc(strlen(string) + 1);

    for (s = string, r = result; *s; ++s)
    {
	if ('%' == *s)
	{
	    if (s[1] && s[2])		/* incomplete sequence? */
	    {
		*r++ = (Hex2Bin(s[1]) << 4) | Hex2Bin(s[2]);
		s += 2;
	    }
	    else
	    {
		free(result);
		return NULL;		/* error! */
	    }
	}
	else
	{
	    if ('+' == *s)
	    {
		*r++ = ' ';
	    }
	    else 
	    {
		*r++ = *s;
	    }
	}
    }

    *r = '\0';				/* terminate string */

    DBUG_RETURN(result);
}

/*
 * EOF
 */
