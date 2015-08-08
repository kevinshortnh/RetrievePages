#ifndef URLPARSE_H
#define URLPARSE_H 1
/*------------------------------------------------------------------------------
 * UrlParse.h -- parse URLs
 *
 * Copyright (c) 2011 Kevin Short.
 */

struct url_parse
{
    char* scheme;
    char* username;
    char* password;
    char* domain;
    char* port;
    char* path;
    char* query_string;
    char* fragment_id;
};
typedef struct url_parse UrlParse_t;

extern UrlParse_t* UrlParse(char* url);
extern void UrlParseFree(UrlParse_t* rpURL);
extern void UrlParsePrint(UrlParse_t* url);

#endif
