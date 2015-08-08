# Retrieve Pages

This is a C Programming Language implementation of a coding assignment I was
given by a former employer.

## Coding Assignment

* Write a utility program to retrieve a specified web page (or pages). The page
  can be displayed on on the terminal or saved to a file.  This utility should
  be able to support HTTP 1.1 keep alive in order to retrieve several pages
  over a single TCP connection.

* This program can be as simple or involved as you like. We'll be looking for
  clarity of code, error handling and efficiency (where appropriate).  We'll
  want to be able to compile and run the code on "some unix" platform, so the
  code should be portable.

* Let me know if you have any questions, or additional ideas for functionality,
  and I'll be happy to provide more input.  If you use any code from software
  you didn't write yourself, please note it in the code.

## Quick Start

* Try 'make test'. Then view the "__delete_me__.*" output files.

* Other 'make' targets include: debug, release, clean, distclean, all, default.

## Introduction

I wrote an "rp" (Retrieve Pages) command line utility.

```
    $ ./rp --help
    Usage: rp [options] url ...
	-h --help              Print this message
	-4 --ipv4              Use IPv4 only
	-6 --ipv6              Use IPv6 only
	-o --output <filename> Specify output filename
	-v --verbose           Enable verbose messages
	-V --version           Print version info
	-# --dbug <state>      Specify DBUG state (development and test)
	
	All retrieved pages are written to the standard output by default.
	
	If fewer output filenames are specified than are URLs, all remaining
	pages are written to the standard output.
	
	When a sequence of URLs refer to the same remote server, HTTP 1.1
	pipelining is used for more efficient retrieval.
	
	See "dbug.c" for details on specifying DBUG state.
	(Specifiying "-# d:t" is a good start.)
    $ 
```

## Built in debugging

* I used the DBUG (dbug.[ch]) package from the old "Fred Fish disks". It
  provides a lot of useful debugging features. See dbug.c for many command line
  options.

## Libraries and System Calls

* I used only system calls and standard library calls.

## URL Encoding

* I wrote a small URL encode/decode (UrlEncode.[ch]) module, to handle the case
  where the spcified URL may be encoded. I really only need to decode, but I
  provided the encode routine as well. Better for testing.

## URL Parsing

* I wrote a small URL parsing (UrlParse.[ch]) module. It handles a limited set
  of URL variants, hopefully sufficient for this assignment.

## Environment

* I tested the utility on CentOS 5.7, Ubuntu 11.10, FreeBSD 8.2, and NetBSD
  5.1.

## Examples

Here are examples of how I tested.

* Bash:

```
    $ ./rp \
	http://www.oxmicro.com/ks-resume/kevinshort.{html,ps,pdf} \
	--verbose \
	--ipv4 \
	-# d:t  \
	-o xx.html \
	-o xx.ps \
	-o xx.pdf \
	http://www.fortinet.com/products/fortiadc/index.html \
	-o xx.2.pdf \
    	2> stderr.log
```

* Bourne Shell:

```
    $ ./rp \
	http://www.oxmicro.com/ks-resume/kevinshort.html \
	http://www.oxmicro.com/ks-resume/kevinshort.ps \
	http://www.oxmicro.com/ks-resume/kevinshort.pdf \
	--verbose \
	--ipv4 \
	-# d:t  \
	-o xx.html \
	-o xx.ps \
	-o xx.pdf \
	http://www.fortinet.com/products/fortiadc/index.html \
	-o xx.2.pdf \
    	2> stderr.log
```

# Design choices

* I check every return value from my own functions calls, system calls, and
  stadard library calls. For a utility of this size, I did not find it
  necessary build an elaborate error handling infrastructure.

* Other design choices are documented in the code.

# Known Bugs

* The application works as expected on CentOS, Ubuntu, and FreeBSD.
    
* On NetBSD it fails at some pages if we do not specify '--ipv4'.  As designed,
  it attempts each discovered address: so it first attempts the IPv6 address;
  next, the IPv4 address -- but it fails to connect on the IPv4 address! If we
  specify '--ipv4', thus preventing it from attempting an IPv6 connection, it
  succeeds.
    
# Copyright (c) 2011 Kevin Short.

* Not for any legal reason; just to let you know I think of such details.

EOF
