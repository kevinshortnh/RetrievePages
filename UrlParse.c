/*------------------------------------------------------------------------------
 * UrlParse.c -- parse URLs
 *
 * Copyright (c) 2011 Kevin Short.
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "UrlParse.h"
#include "dbug.h"

/*------------------------------------------------------------------------------
 * UrlParse() - parse URL
 *
 *  scheme://username:password@domain:port/path?query_string#fragment_id
 *
 * Returns a 'UrlParse_t*' on success, NULL on failure.
 *
 * CAVEAT: Call 'UrlParseFree()' to free dynamically allocated memory.
 *
 * CAVEAT: This function has not been tested on a wide variety of URLs.
 *
 * NOTES: Other design considerations might be (a) actually writing a small
 * flex/bison module for the parsing; (b) using a proliferation of sscanf()
 * calls. Although I find it somewhat ugly, I chose the if/else sequence
 * below.
 */
UrlParse_t* UrlParse(char* url)
{
    UrlParse_t* parsed;

    int len;

    char* p;
    char* pColon;
    char* pAtSign;
    char* pSlash;

    DBUG_ENTER("UrlParse");

    assert(NULL != url);

    parsed = calloc(1, sizeof(UrlParse_t));

    DBUG_PRINT("URL", (url));

    /*
     * scheme
     */

    if (NULL == (p = strstr(url, "://")))
    {
	parsed->scheme = strdup("http"); /* default */
    }
    else
    {
	len = p - url;
	parsed->scheme = malloc(len + 1);
	memcpy(parsed->scheme, url, len);
	parsed->scheme[len] = '\0';
	url = p + 3;
    }
    DBUG_PRINT("URL", ("scheme=%s", parsed->scheme));

    /*
     * test for domain only
     */

    if (NULL == strpbrk(url,":@/"))
    {
	/* assumption: all we have left is the domain */
	parsed->domain = strdup(url);
	parsed->port   = strdup("80");	/* assign default */
	parsed->path   = strdup("/");/* assign default */
	DBUG_PRINT("URL",
	           ("domain=%s port=%s path=%s",
		   parsed->domain,
		   parsed->port,
		   parsed->port));

	DBUG_RETURN(parsed);
    }

    /*
     * username and password
     */

    pColon  = strchr(url, ':');
    pAtSign = strchr(url, '@');

    if ((NULL != pColon) && (NULL != pAtSign) && (pColon < pAtSign))
    {
	/* username */
	len = pColon - url;
	parsed->username = malloc(len + 1);
	memcpy(parsed->username, url, len);
	parsed->username[len] = '\0';
	url += len + 1;
	DBUG_PRINT("URL", ("username=%s", parsed->username));

	/* password */
	len = pAtSign - url;
	parsed->password = malloc(len + 1);
	memcpy(parsed->password, url, len);
	parsed->password[len] = '\0';
	url += len + 1;
	DBUG_PRINT("URL", ("password=%s", parsed->password));
    }
    else
    {
	if (NULL != pAtSign)
	{
	    /* username, but no password */
	    len = pAtSign - url;
	    parsed->username = malloc(len + 1);
	    memcpy(parsed->username, url, len);
	    parsed->username[len] = '\0';
	    url += len + 1;
	    DBUG_PRINT("URL", ("username=%s", parsed->username));
	}
    }

    /*
     * domain and port
     */

    pColon = strchr(url, ':');
    pSlash = strchr(url, '/');

    if (NULL != pColon)
    {
	/* domain */
	len = pColon - url;
	parsed->domain = malloc(len + 1);
	memcpy(parsed->domain, url, len);
	parsed->domain[len] = '\0';
	url += len + 1;
	DBUG_PRINT("URL", ("domain=%s", parsed->domain));

	if (NULL == pSlash)
	{
	    /* ASSUMPTION: all we have left is the port */
	    parsed->port = strdup(url);
	    parsed->path = strdup("/");/* assign default */
	    DBUG_PRINT("URL", ("port=%s path=%s", parsed->port, parsed->path));

	    DBUG_RETURN(parsed);
	}
	else
	{
	    /* port */
	    len = pSlash - url;
	    parsed->port = malloc(len + 1);
	    memcpy(parsed->port, url, len);
	    parsed->port[len] = '\0';
	    url += len + 1;
	    DBUG_PRINT("URL", ("port=%s", parsed->port));
	}
    }
    else
    {
	if (NULL == pSlash)
	{
	    /* assumption: all we have left is the domain */
	    parsed->domain = strdup(url);
	    parsed->port   = strdup("80"); /* assign default */
	    parsed->path   = strdup("/");/* assign default */
	    DBUG_PRINT("URL",
		       ("domain=%s port=%s path=%s",
		       parsed->domain,
		       parsed->port,
		       parsed->path));

	    DBUG_RETURN(parsed);
	}
	else
	{
	    /* domain */
	    len = pSlash - url;
	    parsed->domain = malloc(len + 1);
	    memcpy(parsed->domain, url, len);
	    parsed->domain[len] = '\0';
	    url += len + 1;
	    parsed->port   = strdup("80"); /* assign default */
	    DBUG_PRINT("URL",
	               ("domain=%s port=%s", parsed->domain, parsed->port));
	}
    }

    /*
     * path
     *
     * assumption: only the path remains.
     *
     * No special handling for query string or fragment id.
     */
    parsed->path = strdup(url-1);	/* include the leading '/' */
    DBUG_PRINT("URL", ("path=%s", parsed->path));

    DBUG_RETURN(parsed);
}

/*------------------------------------------------------------------------------------------
 * UrlParseFree() - free a parsed URL
 */
void UrlParseFree(UrlParse_t* parsed)
{
    DBUG_ENTER("UrlParseFree");

    if (NULL == parsed)
    {
	return;
    }

    if (NULL != parsed->scheme)
    {
	free(parsed->scheme);
    }

    if (NULL != parsed->username)
    {
	free(parsed->username);
    }

    if (NULL != parsed->password)
    {
	free(parsed->password);
    }

    if (NULL != parsed->domain)
    {
	free(parsed->domain);
    }

    if (NULL != parsed->port)
    {
	free(parsed->port);
    }

    if (NULL != parsed->path)
    {
	free(parsed->path);
    }

    if (NULL != parsed->query_string)
    {
	free(parsed->query_string);
    }

    if (NULL != parsed->fragment_id)
    {
	free(parsed->fragment_id);
    }

    DBUG_VOID_RETURN;
}

/*----------------------------------------------------------------------------------------------
 * UrlParsePrint() - print a parsed URL
 */
void UrlParsePrint(UrlParse_t* parsed)
{
    DBUG_ENTER("UrlParsePrint");

    if (NULL != parsed)
    {
	DBUG_PRINT("UrlParse",("%s://%s:%s@%s:%s/%s?%s#%s",
	    parsed->scheme,
	    parsed->username,
	    parsed->password,
	    parsed->domain,
	    parsed->port,
	    parsed->path,
	    parsed->query_string,
	    parsed->fragment_id));
    }

    DBUG_VOID_RETURN;
}

/*
 * EOF
 */
