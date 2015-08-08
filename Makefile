#
# Makefile for "rp" utility
#
# Targets:
#
#  release	Release build, DBUG disabled.
#  debug	Debug build,   DBUG enabled.
#
#  clean	Clean the objects.
#  distclean	Clean everything.
#
#  test		Test the 'rp' program.
#  ue_test	Unit test for 'UrlEncode' module.
#
# Copyright (c) 2011 Kevin Short.
#

RM		= /bin/rm -rf

prog		= rp
srcs		= rp.c UrlEncode.c UrlParse.c dbug.c
incs		=      UrlEncode.h UrlParse.h dbug.h
objs		= $(srcs:.c=.o)

ue_prog		= UrlEncodeTest
ue_srcs		= UrlEncodeTest.c UrlEncode.c dbug.c
ue_incs		=                 UrlEncode.h dbug.h
ue_objs		= $(ue_srcs:.c=.o)

deleteme	= __delete_me__

.PHONY: all default debug release test ue_test clean distclean

all default: debug

release:	$(prog)
release:	CPPFLAGS += -DDBUG_OFF

debug:		$(prog)
debug:		CPPFLAGS += -UDBUG_OFF
debug:          CFLAGS += -Wall --pedantic

clean:
	$(RM) $(objs) $(ue_objs) $(deleteme).*

distclean:
	$(RM) $(objs) $(prog) $(ue_objs) $(ue_prog) $(deleteme).*

$(prog): $(objs)
	@echo "NOTE: PLEASE IGNORE WARNING PER EXTERNAL LIBRARY dbug.c"
	$(CC) -o $(prog) $(objs)

$(objs): $(incs)

test: $(prog)
	./rp \
		--verbose \
		-# d:t \
		http://www.fortinet.com/products/fortiadc/index.html \
		-o $(deleteme).php \
		# 2>$(deleteme).log

test2: $(prog)
	./rp \
		--verbose \
		--ipv4 \
		-# d:t \
		http://www.oxmicro.com/ks-resume/kevinshort.html \
		http://www.oxmicro.com/ks-resume/kevinshort.ps \
		http://www.oxmicro.com/ks-resume/kevinshort.pdf \
		-o $(deleteme).html \
		-o $(deleteme).ps \
		-o $(deleteme).pdf \
		http://www.fortinet.com/products/fortiadc/index.html \
		-o $(deleteme).2.pdf \
		2>$(deleteme).log

bashtest: $(prog)
	bash -c "./rp \
		--verbose \
		--ipv4 \
		-# d:t \
		http://www.oxmicro.com/ks-resume/kevinshort.{html,ps,pdf} \
		-o $(deleteme).html \
		-o $(deleteme).ps \
		-o $(deleteme).pdf \
		http://www.fortinet.com/products/fortiadc/index.html \
		-o $(deleteme).2.pdf" \
		2>$(deleteme).log

$(ue_prog): $(ue_objs)
	$(CC) -o $(ue_prog) $(ue_objs)

$(ue_objs): $(ue_incs)

ue_test: $(ue_prog)
	bash -c "./$(ue_prog)"

# EOF
