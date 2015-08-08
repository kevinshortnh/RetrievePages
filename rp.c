/*------------------------------------------------------------------------------
 * rp.c - utility to Retrieve web Pages.
 *
 * See usage() for accurate details.
 *
 * Copyright (c) 2011 Kevin Short.
 */

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include "UrlEncode.h"
#include "UrlParse.h"
#include "dbug.h"

#define RP_VERSION "1.0.0"

/*
 * Function return codes
 */
enum rp_result
{
    rp_success = 0,
    rp_failure = -1
};
typedef enum rp_result rpResult_t;

/*
 * Indicates socket is closed
 */
#define RP_SOCK_CLOSED (-1)

/*
 * HTTP Request header components
 */
#define HTTP_GET		"GET "
#define HTTP_HTTP_1_1		" HTTP/1.1\r\n"
#define HTTP_HOST    		"Host: "
#define HTTP_CONNECTION		"Connection: keep-alive\r\n"
#define HTTP_CACHE_CONTROL	"Cache-Control: no-cache\r\n"
#define HTTP_CRLF    		"\r\n"

/*
 * HTTP Respone header components
 */
#define HTTP_200_OK		"HTTP/1.1 200 OK\r\n"
#define HTTP_CONTENT_LENGTH	"Content-Length: "
#define HTTP_TRANSFER_ENCODING	"Transfer-Encoding: chunked"

/*
 * Command line option settings
 */
struct rp_options
{
    char** urls;			/* URLS */
    char** filenames;			/* output filenames */
    int isVerbose;			/* is verbose mode enabled? */
    int isIPV4only;			/* is IPV4 only mode enabled? */
    int isIPV6only;			/* is IPv6 only mode enabled? */
};
typedef struct rp_options rpOptions_t;

/*
 * Current state
 */
struct rp_state
{
    char** currUrl;			/* current URL */
    char** currFilename;		/* current filename */
    char* so_rcvbuf;			/* socket receive buffer */
    char* pBuf;				/* ptr to data in so_rcvbuf */
    long contentLength;			/* content length */
    socklen_t so_rcvbuf_len;		/* socket receive buffer length */
    int sock;				/* the socket */
    int fd;				/* the file */
    int pipeline;			/* pipelined requests */
    int bytes;				/* bytes in buffer */
};
typedef struct rp_state rpState_t;

/*------------------------------------------------------------------------------
 * getChunkSize() - get a chunk size
 */
static rpResult_t getChunkSize(char** p, long* chunkSize)
{
    char* t;
    long chunk = 0;

    DBUG_ENTER("getChunkSize");

    assert(NULL != p);
    assert(NULL != chunkSize);

    for (t = *p; '\r' != *t; ++t)
    {
	if (isdigit(*t))
	{
	    chunk = (chunk << 4) | (*t - '0');
	}
	else
	{
	    char ch = tolower(*t);

	    if (('a' <= ch) && ('f' >= ch))
	    {
		chunk = (chunk << 4) | (10 + ch - 'a');
	    }
	    else
	    {
		break;			/* could be "; options */
	    }
	}
    }

    /* find CR/LF */
    if (NULL == (t = strstr(t, HTTP_CRLF)))
    {
	DBUG_RETURN(rp_failure);
    }

    *p = t + 2;				/* walk past CR/LF */

    *chunkSize = chunk;

    DBUG_PRINT("chunk", ("0x%lx %ld.", chunk, chunk));

    DBUG_RETURN(rp_success);
}

/*------------------------------------------------------------------------------
 * processTransferEncodingResponse()
 */
static rpResult_t processTransferEncodingResponse(
	rpOptions_t* options,
	rpState_t* state)
{
#define RP_EXPECT_CHUNK_SIZE (-1)
#define RP_EXPECT_CHUNK_TERMINATOR (-2)

    long chunk = RP_EXPECT_CHUNK_SIZE;

    int rc;

    int bytesWritten;

    DBUG_ENTER("processTransferEncodingResponse");

    assert(NULL != options);
    assert(NULL != state);

    /*
     * See if we have bytes left in the buffer, following the headers
     */

    if (state->bytes > 0)
    {
	char* t = state->pBuf;

	if (rp_success != getChunkSize(&state->pBuf, &chunk))
	{
	    DBUG_RETURN(rp_failure);
	}
	state->bytes -= (state->pBuf - t);

	if (0 == chunk)
	{
	    /* we are done */
	    DBUG_RETURN(rp_success);
	}

	bytesWritten = (state->bytes < chunk) ? state->bytes : chunk;

	if (-1 == (rc = write(state->fd, state->pBuf, bytesWritten)))
	{
	    perror("write(fd)");

	    DBUG_PRINT("syscall", ("write(fd) failed"));

	    DBUG_RETURN(rp_failure);
	}
	chunk -= bytesWritten;		/* remaining chunk length */

	DBUG_PRINT("response",
		  ("wrote %10d, chunk %10ld", bytesWritten, chunk));

	if (state->bytes > bytesWritten)
	{
	    /*
	     * The header of the next response is already in our buffer, so we
	     * will move the response to the front of the buffer and setup a
	     * short read, to fill the buffer.
	     */

	    state->bytes -= bytesWritten;
	    memcpy(state->so_rcvbuf, &state->pBuf[bytesWritten], state->bytes);

	    /* setup for a short read */
	    state->pBuf  = &state->so_rcvbuf[state->bytes];
	    state->bytes = state->so_rcvbuf_len - state->bytes;
	}
    }
    else
    {
	/* setup for a full read */
	state->pBuf  = state->so_rcvbuf;
	state->bytes = state->so_rcvbuf_len;
    }

    /*
     * Process the remainder of the current response
     */

    while (1)
    {
	int bytesWritten;

	DBUG_PRINT("response",
		  ("max   %10d, chunk %10ld", state->bytes, chunk));

	if (-1 == (state->bytes = read(state->sock, state->pBuf, state->bytes)))
	{
	    perror("read(sock)");

	    DBUG_PRINT("syscall", ("read(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	DBUG_PRINT("response",
		  ("read  %10d, offset %d",
		  state->bytes,
		  state->pBuf - state->so_rcvbuf));

	state->pBuf = state->so_rcvbuf; /* reset to start of buffer */

	if (0 == state->bytes)
	{
	    break;
	}

	bytesWritten = state->bytes;


	if (RP_EXPECT_CHUNK_TERMINATOR == chunk)
	{
	    /* expect CRLF */
	    if (('\r' == state->pBuf[0]) && ('\n' == state->pBuf[1]))
	    {
		state->pBuf  += 2;
		state->bytes -= 2;
	    }
	    else
	    {
		int i;

		if (options->isVerbose)
		{
		    printf("Malformed chunk: did not end with CR/LF -- stopping.\n");
		}

		for (i = 0; (i < state->bytes) && (i < 16); ++i)
		{
		    DBUG_PRINT("chunkTerminator",
			      ("0x%02.2x %d", state->pBuf[i], i));
		}

		DBUG_RETURN(rp_failure);
	    }

	    chunk = RP_EXPECT_CHUNK_SIZE;
	}

	if (RP_EXPECT_CHUNK_SIZE == chunk)
	{
	    /*
	     * Expect a chunk size
	     */

	    char* t = state->pBuf;

	    if (rp_success != getChunkSize(&state->pBuf, &chunk))
	    {
		DBUG_RETURN(rp_failure);
	    }
	    state->bytes -= (state->pBuf - t);

	    if (0 == chunk)
	    {
		/* we are done */
		DBUG_RETURN(rp_success);
	    }

	    if (state->bytes > chunk)
	    {
		/* buffer contains part of next response */
		bytesWritten = chunk;
	    }
	}

	if (-1 == (rc = write(state->fd, state->pBuf, bytesWritten)))
	{
	    perror("write(fd)");

	    DBUG_PRINT("syscall", ("write(fd) failed"));

	    DBUG_RETURN(rp_failure);
	}

	state->pBuf  += bytesWritten;
	state->bytes -= bytesWritten;

	chunk -= bytesWritten;

	DBUG_PRINT("response",
		  ("wrote %10d, chunk %10ld", bytesWritten, chunk));

	if (0 == chunk)
	{
	    memcpy(state->so_rcvbuf, state->pBuf, state->bytes);

	    chunk = RP_EXPECT_CHUNK_TERMINATOR;

	    /* setup for a short read */
	    state->pBuf  = &state->so_rcvbuf[state->bytes];
	    state->bytes = state->so_rcvbuf_len - state->bytes;
	}
	else
	{
	    /* setup for a full, or end of chunk, read */
	    state->pBuf  = state->so_rcvbuf;
	    state->bytes = (0 < chunk) && (chunk < state->so_rcvbuf_len)
		? chunk
		: state->so_rcvbuf_len;
	}
    }

    /* setup for a full read */
    state->pBuf  = state->so_rcvbuf;
    state->bytes = state->so_rcvbuf_len;

    DBUG_RETURN(rp_success);
}

/*------------------------------------------------------------------------------
 * processContentLengthResponse()
 */
static rpResult_t processContentLengthResponse(
	rpOptions_t* options,
	rpState_t* state)
{
    int rc;
    int bytesWritten;

    DBUG_ENTER("processContentLengthResponse");

    assert(NULL != options);
    assert(NULL != state);

    /*
     * See if we have bytes left in the buffer, following the headers
     */

    bytesWritten = state->bytes;

    if (bytesWritten > state->contentLength)
    {
	/* buffer contains part of next response */
	bytesWritten = state->contentLength;
    }

    DBUG_PRINT("response",
	      ("wrote %10d, contentLength %10d",
	      bytesWritten,
	      state->contentLength));

    if (-1 == (rc = write(state->fd, state->pBuf, bytesWritten)))
    {
	perror("write(fd)");

	DBUG_PRINT("syscall", ("write(fd) failed"));

	DBUG_RETURN(rp_failure);
    }

    state->contentLength -= bytesWritten;/* remaining content length */

    if (state->bytes > bytesWritten)
    {
	/*
	 * The header of the next response is already in our buffer, so we
	 * will move the response to the front of the buffer and setup a
	 * short read, to fill the buffer.
	 */

	state->bytes -= bytesWritten;
	memcpy(state->so_rcvbuf, &state->pBuf[bytesWritten], state->bytes);

	/* setup for a short read */
	state->pBuf  = &state->so_rcvbuf[state->bytes];
	state->bytes = state->so_rcvbuf_len - state->bytes;

	DBUG_RETURN(rp_success);
    }

    /*
     * Process the remainder of the current response
     */

    while (state->contentLength > 0)
    {
	state->pBuf = state->so_rcvbuf;

	state->bytes = (state->contentLength < state->so_rcvbuf_len)
	    ? state->contentLength
	    : state->so_rcvbuf_len;

	DBUG_PRINT("response",
		  ("max   %10d, contentLength %10d",
		  state->bytes,
		  state->contentLength));

	if (-1 == (state->bytes = read(state->sock, state->pBuf, state->bytes)))
	{
	    perror("read(sock)");

	    DBUG_PRINT("syscall", ("read(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	DBUG_PRINT("response", ("read  %10d", state->bytes));

	if (0 == state->bytes)
	{
	    break;
	}

	if (-1 == (rc = write(state->fd, state->pBuf, state->bytes)))
	{
	    perror("write(fd)");

	    DBUG_PRINT("syscall", ("write(fd) failed"));

	    DBUG_RETURN(rp_failure);
	}

	state->contentLength -= state->bytes;

	DBUG_PRINT("response",
		  ("wrote %10d, contentLength %10d",
		  state->bytes,
		  state->contentLength));
    }

    /* setup for a full read */
    state->pBuf  = state->so_rcvbuf;
    state->bytes = state->so_rcvbuf_len;

    DBUG_RETURN(rp_success);
}

/*------------------------------------------------------------------------------
 * processResponses() - process the [pipelined] HTTP responses
 */
static rpResult_t processResponses(rpOptions_t* options, rpState_t* state)
{
    rpResult_t result = rp_success;

    DBUG_ENTER("processResponses");

    assert(NULL != options);
    assert(NULL != state);

    /*
     * Process the responses.
     *
     * We must handle a mix of responses with either Content-Length or
     * Transfer-Encoding.
     */

    for (state->pBuf = state->so_rcvbuf, state->bytes = state->so_rcvbuf_len;
         state->pipeline > 0;
	 --state->pipeline)
    {
	int isTransferEncoding = 0;

	int len;
	int rc;

	char* t;			/* temporary buffer pointer */

	state->contentLength = -1;

	/*
	 * Process the response headers
	 */

	if (-1 == (state->bytes = read(state->sock, state->pBuf, state->bytes)))
	{
	    perror("read(sock)");

	    DBUG_PRINT("syscall", ("read(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	DBUG_PRINT("responseHeader", ("bytes %d", state->bytes));

	state->pBuf[state->bytes] = '\0';/* terminate string */

	/*
	 * First response header ought to indicate everything is okay
	 */

	len = strlen(HTTP_200_OK);

	if (0 != strncmp(state->pBuf, HTTP_200_OK, len))
	{
	    DBUG_PRINT("responseHeader", ("failed -- not 200"));
	    DBUG_PRINT("responseHeader", ("%s", state->pBuf));

	    fprintf(stderr, "HTTP request failed.\n");

	    if (options->isVerbose)
	    {
		printf("%s\n", state->pBuf);
	    }

	    DBUG_RETURN(rp_failure);
	}

	state->pBuf += len;

	/*
	 * Process remaining response headers
	 */

	while (1)
	{
	    if ((state->pBuf >= &state->so_rcvbuf[state->so_rcvbuf_len])
	    ||  (NULL == (t = strstr(state->pBuf, HTTP_CRLF))))
	    {
		/*
		 * ASSUMPTION: All the response headers will fit in one buffer.
		 */

		fprintf(stderr, "Response headers did not fit in buffer.\n");

		DBUG_RETURN(rp_failure);
	    }

	    if (t == state->pBuf)	/* found response terminator */
	    {
		state->pBuf = t + 2;	/* skip past CR/LF */
		break;
	    }

	    len = strlen(HTTP_CONTENT_LENGTH);
	    if (0 == strncmp(state->pBuf, HTTP_CONTENT_LENGTH, len))
	    {
		/* content length */
		state->contentLength = atoi(state->pBuf + len);
		DBUG_PRINT("responseHeader",
			  ("%s %d", HTTP_CONTENT_LENGTH, state->contentLength));
	    }
	    else
	    {
		len = strlen(HTTP_TRANSFER_ENCODING);
		if (0 == strncmp(state->pBuf, HTTP_TRANSFER_ENCODING, len))
		{
		    /* transfer enconding */
		    isTransferEncoding = 1;
		    DBUG_PRINT("responseHeader", ("%s", HTTP_TRANSFER_ENCODING));
		}
	    }

	    /*
	     * All other reponse headers are ignored
	     */

	    state->pBuf = t + 2;	/* walk past CR/LF */
	}

	/* decrement remaining byte count by header size */
	state->bytes -= state->pBuf - state->so_rcvbuf;

	if (isTransferEncoding && (state->contentLength >= 0))
	{
	    fprintf(stderr,
		    "Received both %s and %s -- stopping.\n",
		    HTTP_CONTENT_LENGTH,
		    HTTP_TRANSFER_ENCODING);

	    DBUG_RETURN(rp_failure);
	}

	/*
	 * We are past the response headers.
	 *
	 * Open the output file, if specified.
	 */

	state->fd = STDOUT_FILENO;	/* defaults to stdout */

	if (NULL != *state->currFilename)
	{
	    if (options->isVerbose)
	    {
		printf("Output %s\n", *state->currFilename);
	    }

	    /* TODO: review mode 0666 */
	    if (-1 == (state->fd =
		    open(*state->currFilename, O_CREAT|O_RDWR, 0666)))
	    {
		perror("open()");

		DBUG_PRINT("syscall",
			   ("open() failed for %s", *state->currFilename));

		DBUG_RETURN(rp_failure);
	    }

	    ++state->currFilename;	/* walk to next filename */
	}

	/*
	 * Process the response data
	 */

	if (isTransferEncoding)
	{
	    if (rp_success != processTransferEncodingResponse(options, state))
	    {
		DBUG_RETURN(rp_failure);
	    }
	}
	else
	{
	    if (rp_success != processContentLengthResponse(options, state))
	    {
		DBUG_RETURN(rp_failure);
	    }
	}

	if (STDOUT_FILENO != state->fd)
	{
	    if (-1 == (rc = close(state->fd)))
	    {
		perror("close(fd)");

		DBUG_PRINT("syscall", ("close(fd) failed"));

		result = rp_failure;
	    }
	}
	else
	{
	    ;				/* could flush stdout */
	}
    }

    DBUG_RETURN(result);
}

/*------------------------------------------------------------------------------
 * sendRequests() send the [pipelined] HTTP requests
 */
static rpResult_t sendRequests(rpOptions_t* options, rpState_t* state)
{
    rpResult_t result = rp_success;

    UrlParse_t* parsed;

    DBUG_ENTER("sendRequests");

    assert(NULL != options);
    assert(NULL != state);

    /*
     * Make pipelined requests
     *
     * NOTE: Could concatenate some request strings, for fewer write()'s to
     *       socket. Chose to leave them separate for now -- for clarity.
     */

    state->pipeline = 1;

    parsed = UrlParse(*state->currUrl);

    while (1)
    {
	UrlParse_t* prevUrl;

	int rc;

	if (options->isVerbose)
	{
	    printf("Path   %s\n", parsed->path);
	}

	/*
	 * GET ...
	 */

	if (-1 == (rc = write(state->sock,
	                      HTTP_GET,
			      strlen(HTTP_GET))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	if (-1 == (rc = write(state->sock,
	                      parsed->path,
			      strlen(parsed->path))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	if (-1 == (rc = write(state->sock,
	                      HTTP_HTTP_1_1,
			      strlen(HTTP_HTTP_1_1))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	/*
	 * Host: ...
	 */

	if (-1 == (rc = write(state->sock,
	                      HTTP_HOST,
			      strlen(HTTP_HOST))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	if (-1 == (rc = write(state->sock,
	                      parsed->domain,
			      strlen(parsed->domain))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	if (-1 == (rc = write(state->sock,
	                      HTTP_CRLF,
			      strlen(HTTP_CRLF))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	/*
	 * Connection: ... (may not be necessary)
	 */

	if (-1 == (rc = write(state->sock,
	                      HTTP_CONNECTION,
			      strlen(HTTP_CONNECTION))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	/*
	 * Cache-control: ... (may not be necessary)
	 */

	if (-1 == (rc = write(state->sock,
	                      HTTP_CACHE_CONTROL,
			      strlen(HTTP_CACHE_CONTROL))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	/*
	 * Terminate with blank line
	 */

	if (-1 == (rc = write(state->sock,
	                      HTTP_CRLF,
			      strlen(HTTP_CRLF))))
	{
	    perror("write(sock)");

	    DBUG_PRINT("syscall", ("write(sock)"));

	    DBUG_RETURN(rp_failure);
	}

	/*
	 * Look for opportunity to pipeline requests
	 */

	prevUrl = parsed;		/* remember */

	++state->currUrl;		/* walk to next URL */

	if (NULL != *state->currUrl)
	{
	    parsed = UrlParse(*state->currUrl);

	    if ((0 == strcmp(parsed->domain, prevUrl->domain))
	    &&  (0 == strcmp(parsed->port,   prevUrl->port)))
	    {
		++state->pipeline;	/* same server */

		if (options->isVerbose)
		{
		    printf("       ...pipelining next request\n");
		}

		DBUG_PRINT("request", ("pipeline %d", state->pipeline));

		UrlParseFree(prevUrl);	/* cleanup */
	    }
	    else
	    {
		break;			/* different server */
	    }
	}
	else
	{
	    break;			/* final URL */
	}
    }

    UrlParseFree(parsed);		/* cleanup */

    DBUG_RETURN(result);
}

/*------------------------------------------------------------------------------
 * processConnection() - process the connection
 */
static rpResult_t processConnection(rpOptions_t* options, rpState_t* state)
{
    rpResult_t result = rp_success;

    DBUG_ENTER("processConnection");

    if (rp_success == (result = sendRequests(options, state)))
    {
	result = processResponses(options, state);
    }

    DBUG_RETURN(result);
}

/*------------------------------------------------------------------------------
 * retrievePages() - retrieve one or more web pages
 */
static rpResult_t retrievePages(rpOptions_t* options, rpState_t* state)
{
    rpResult_t result = rp_success;

    int rc;				/* return codes from system calls */

    DBUG_ENTER("retrievePages");

    assert(NULL != options);
    assert(NULL != state);

    /*
     * Each URL
     *
     * We expect 'processConnection()' to walk to next URL.
     */

    state->sock         = RP_SOCK_CLOSED;
    state->currUrl      = options->urls;
    state->currFilename = options->filenames;

    while ((NULL != *state->currUrl) && (rp_success == result))
    {
	void* voidp;

	struct addrinfo* p;
	struct addrinfo* serverInfo;
	struct addrinfo serverHints;

	int isConnected;

	UrlParse_t* parsed = UrlParse(*state->currUrl);

	if (options->isVerbose)
	{
	    printf("Server %s\n", parsed->domain);
	}

	/*
	 * Lookup host
	 */

	voidp = memset((void*)&serverHints, 0, sizeof serverHints);

	assert((void*)&serverHints == voidp);

	serverHints.ai_socktype = SOCK_STREAM;/* TCP */

	if (options->isIPV4only)
	{
	    serverHints.ai_family = AF_INET;/* only IPv4 */
	}
	else
	{
	    if (options->isIPV6only)
	    {
		serverHints.ai_family = AF_INET6;/* only IPv6 */
	    }
	    else
	    {
		serverHints.ai_family = AF_UNSPEC;/* IPV4 or IPv6 */
	    }
	}

	serverInfo = NULL;

	if (0 != (rc = getaddrinfo(parsed->domain,
				   parsed->port,
				   &serverHints,
				   &serverInfo)))
	{
	    fprintf(stderr,
		    "getaddrinfo(): %s %s\n",
		    parsed->domain,
		    gai_strerror(rc));

	    DBUG_PRINT("library",
	               ("getaddrinfo() failed for %s:%s",
		       parsed->domain,
		       parsed->port));

	    result = rp_failure;	/* fatal */
	}

	/*
	 * Attempt to connect at each address on this server,
	 * until successful
	 */

	for (p = serverInfo, isConnected = 0;
	     (NULL != p) && (rp_success == result) && !isConnected;
	     p = p->ai_next)
	{
	    void* addr;
	    char* ipVersion;
	    char printableAddress[INET6_ADDRSTRLEN];

	    if (AF_INET == p->ai_family)/* IPv4 */
	    {
		struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
		addr = &ipv4->sin_addr;
		ipVersion = "IPv4";
	    }
	    else			/* IPv6 */
	    {
		struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)p->ai_addr;
		addr = &ipv6->sin6_addr;
		ipVersion = "IPv6";
	    }

	    inet_ntop(p->ai_family,
		      addr,
		      printableAddress,
		      sizeof printableAddress);

	    if (options->isVerbose)
	    {
		printf("%-6s %s\n", ipVersion, printableAddress);
	    }

	    /*
	     * Open the socket
	     */

	    if (-1 == (state->sock = socket(serverInfo->ai_family,
				     serverInfo->ai_socktype,
				     serverInfo->ai_protocol)))
	    {
		perror("socket()");

		DBUG_PRINT("syscall", ("socket() failed"));
		
		result = rp_failure;
	    }
	    else
	    {
		/*
		 * Set optimal socket receive buffer size.
		 *
		 * (The remote server may use a different size.)
		 */

		unsigned long len = 0;
		socklen_t size = sizeof len;

		if (-1 == (rc = getsockopt(state->sock,
					   SOL_SOCKET,
					   SO_RCVBUF,
					   &len,
					   &size)))
		{
		    perror("getsockopt()");

		    DBUG_PRINT("syscall", ("getsockopt() failed"));
		}
		else
		{
		    DBUG_PRINT("syscall",
			       ("getsockopt() size %d, len %lu", size, len));
		}

		if (NULL == state->so_rcvbuf)/* first time */
		{
		    state->so_rcvbuf_len = len;

		    if (NULL ==
			    (state->so_rcvbuf = malloc(state->so_rcvbuf_len)))
		    {
			DBUG_PRINT("syslib",
				   ("malloc() failed for so_rcvbuf, size %lu",
				    state->so_rcvbuf_len));

			DBUG_RETURN(rp_failure);
		    }
		}
		else			/* see if we need to grow the buffer */
		{
		    if (len > state->so_rcvbuf_len)
		    {
			free(state->so_rcvbuf);

			state->so_rcvbuf_len = len;

			if (NULL ==
				(state->so_rcvbuf = malloc(state->so_rcvbuf_len)))
			{
			    DBUG_PRINT("syslib",
				       ("malloc() failed for so_rcvbuf, size %lu",
					state->so_rcvbuf_len));

			    DBUG_RETURN(rp_failure);
			}
		    }
		}

		/*
		 * Connect
		 */

		if (-1 == (rc = connect(state->sock,
					serverInfo->ai_addr,
					serverInfo->ai_addrlen)))
		{
		    DBUG_PRINT("syscall",
			       ("connect() failed for %s",
			       printableAddress));
		}
		else
		{
		    isConnected = 1;

		    result = processConnection(options, state);
		}

		/*
		 * Close the socket
		 */

		if (-1 == (rc = close(state->sock)))
		{
		    perror("close(sock)");

		    DBUG_PRINT("syscall", ("close(sock) failed"));

		    result = rp_failure;
		}

		state->sock = RP_SOCK_CLOSED;
	    }
	}

	if (!isConnected)
	{
	    /*
	     * All attempts to connect to this host failed
	     */

	    fprintf(stderr, "Unable to connect to host\n");

	    result = rp_failure;
	}

	freeaddrinfo(serverInfo);	/* cleanup */
    }

    assert(RP_SOCK_CLOSED == state->sock);

    DBUG_RETURN(result);
}

/*------------------------------------------------------------------------------
 * usage() - display usage
 */
static void usage(char* programName)
{
    static char* messages[] =
    {
	"-h --help              Print this message",
	"-4 --ipv4              Use IPv4 only",
	"-6 --ipv6              Use IPv6 only",
	"-o --output <filename> Specify output filename",
	"-v --verbose           Enable verbose messages",
	"-V --version           Print version info",
#ifndef DBUG_OFF
	"-# --dbug <state>      Specify DBUG state (development and test)",
#endif
	"",
	"All retrieved pages are written to the standard output by default.",
	"",
	"If fewer output filenames are specified than are URLs, all remaining",
	"pages are written to the standard output.",
	"",
	"When a sequence of URLs refer to the same remote server, HTTP 1.1",
	"pipelining is used for more efficient retrieval.",
#ifndef DBUG_OFF
	"",
	"See \"dbug.c\" for details on specifying DBUG state.",
	"(Specifiying \"-# d:t\" is a good start.)",
#endif
	NULL,
    };

    char** p;

    DBUG_ENTER("usage");

    printf("Usage: %s [options] url ...\n", basename(programName));

    for (p = messages; NULL != *p; ++p)
    {
	printf("    %s\n", *p);
    }

    DBUG_VOID_RETURN;
}

/*------------------------------------------------------------------------------
 * version() - display version
 */
static void version()
{
    DBUG_ENTER("version");

    printf("Version " RP_VERSION "\n");

    DBUG_VOID_RETURN;
}

/*------------------------------------------------------------------------------
 * initialize() - initialize options, based on command line arguments
 */
static int initialize(int argc, char** argv, rpOptions_t* options)
{
    static const struct option opts[] =
    {
	{ "help",         no_argument, NULL, 'h' },
	{ "ipv4",         no_argument, NULL, '4' },
	{ "ipv6",         no_argument, NULL, '6' },
	{ "output", required_argument, NULL, 'o' },
	{ "verbose",      no_argument, NULL, 'v' },
	{ "version",      no_argument, NULL, 'V' },
#ifndef DBUG_OFF
	{ "dbug",   required_argument, NULL, '#' },
#endif
	{ NULL,           no_argument, NULL, 0 },
    };

    rpResult_t result  = rp_success;

    int isUrlSpecified = 0;
    int isShowVersion  = 0;
    int isShowHelp     = 0;

    char** p;				/* ptr to string arrays */

    int opt;				/* current option, as a single char */

    DBUG_ENTER("initialize");

    /*
     * As a worst case, the user could just specify a bunch of filenames.
     *
     * Allocate more pointers than we need. As this is a transient utility, the
     * cost is minimal.
     */

    p = options->filenames = malloc((argc >> 1) * sizeof(char*) + 1);

    /*
     * Process each command line argument
     */

    while (-1 != (opt = getopt_long(argc, argv, "h46o:vV#:", opts, NULL)))
    {
	switch (opt)
	{
	case 'h':
	    isShowHelp = 1;
	    DBUG_PRINT("cmdline", ("h"));
	    break;

	case '4':
	    options->isIPV4only = 1;
	    DBUG_PRINT("cmdline", ("4"));
	    break;

	case '6':
	    options->isIPV6only = 1;
	    DBUG_PRINT("cmdline", ("6"));
	    break;

	case 'o':
	    if (NULL != optarg)
	    {
		*p++ = strdup(optarg); /* accumulate filenames */
		DBUG_PRINT("cmdline", ("o %s", optarg));
	    }
	    else
	    {
		fprintf(stderr, "Internal error");
		abort();
	    }
	    break;

	case 'v':
	    options->isVerbose = 1;
	    DBUG_PRINT("cmdline", ("v"));
	    break;

	case 'V':
	    isShowVersion = 1;
	    DBUG_PRINT("cmdline", ("V"));
	    break;

#ifndef DBUG_OFF
	case '#':
	    if (NULL != optarg)
	    {
		DBUG_PUSH(optarg);
		DBUG_PRINT("cmdline", ("# %s", optarg));
	    }
	    else
	    {
		fprintf(stderr, "Internal error");
		abort();
	    }
	    break;
#endif

	case '?':
	     ;				/* fall through */

	default:
	     result = rp_failure;
	     break;
	}
    }

    *p = NULL;				/* terminate array of pointers */

    if (optind < argc)			/* remaining arguments are URLs */
    {
	isUrlSpecified = 1;		/* need at least one URL */

	p = options->urls  = malloc((argc - optind + 1) * sizeof(char*));

	while (optind < argc)
	{
	    DBUG_PRINT("cmdline", ("URL %s", argv[optind]));
	    *p++ = UrlDecode(argv[optind++]);/* un-encode and save URL */
	}

	*p = NULL;			/* terminate array of pointers */
    }

    /*
     * Displays
     */

    if (isShowVersion)
    {
	version();
    }

    if (isShowHelp)
    {
	usage(argv[0]);
    }

    if (options->isVerbose)
    {
	printf("verbose mode enabled\n");
    }

    /*
     * Validity checks
     */

    if (options->isIPV4only && options->isIPV6only)
    {
	fprintf(stderr, "Cannot specify IPv4 only and IPv6 only.\n");

	result = rp_failure;
    }

    /*
     * Validate URL scheme
     */

    if (isUrlSpecified)
    {
	for (p = options->urls; NULL != *p; ++p)
	{
	    static char* httpScheme = "http";
	    static char* httpsScheme = "https";

	    UrlParse_t* parsed = UrlParse(*p);
	    
	    if ((0 != strcasecmp(httpScheme, parsed->scheme))
	    &&  (0 != strcasecmp(httpsScheme, parsed->scheme)))
	    {
		fprintf(stderr,
			"Only the %s and %s schemes are supported.\n",
			httpScheme,
			httpsScheme);

		result = rp_failure;
		break;
	    }
	}
    }
    else
    {
	if (!isShowHelp)
	{
	    fprintf(stderr, "Must specify at least one URL.\n");
	}

	result = rp_failure;
    }

    DBUG_RETURN(result);
}

/*------------------------------------------------------------------------------
 * run() - fetch, and optionally save, the pages
 */
static rpResult_t run(rpOptions_t* options, rpState_t* state)
{
    rpResult_t result = rp_success;

    DBUG_ENTER("run");

    result = retrievePages(options, state);

    DBUG_RETURN(result);
}

/*------------------------------------------------------------------------------
 * terminate() - cleanup and exit
 */
static void terminate(rpOptions_t* options, rpState_t* state)
{
    DBUG_ENTER("terminate");

    assert(NULL != options);
    assert(NULL != state);

    if (NULL != options->urls)
    {
	free(options->urls);
    }

    if (NULL != options->filenames)
    {
	free(options->filenames);
    }
    
    if (NULL != state->so_rcvbuf)
    {
	free(state->so_rcvbuf);
    }

    DBUG_VOID_RETURN;
}

/*------------------------------------------------------------------------------
 * The main program
 *
 * No global variables! State is encapsulate in two structs.
 */
int main(int argc, char** argv)
{
    int exitCode = 1;			/* failure */

    rpOptions_t options;
    rpState_t state;

    DBUG_ENTER("main");

    memset(&options, 0, sizeof options);
    memset(&state, 0, sizeof state);

    if (rp_success == initialize(argc, argv, &options))
    {
	if (rp_success == run(&options, &state))
	{
	    exitCode = 0;		/* success */
	}
    }

    terminate(&options, &state);

    DBUG_RETURN(exitCode);
}

/*
 * EOF
 */
