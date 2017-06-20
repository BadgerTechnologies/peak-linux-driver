#****************************************************************************
# Copyright (C) 2001-2007  PEAK System-Technik GmbH
#
# linux@peak-system.com
# www.peak-system.com
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# Maintainer(s): Klaus Hitschler (klaus.hitschler@gmx.de)
#****************************************************************************

#****************************************************************************
#
# Makefile - Makefile for the shared library libpcan.so.x.x
#
# $Id: Makefile 1156 2016-03-08 13:56:05Z stephane $
#
#****************************************************************************

PCANDRV_DIR := ../driver

SRC := src
INC := -I. -I$(PCANDRV_DIR)
FILES := $(SRC)/libpcan.c
FILESFD := $(SRC)/libpcanfd.c
DBG := -g
RT ?= NO_RT

ifeq ($(RT), XENOMAI)
# Define flags for XENOMAI 2.6 versions installation only
# Note:
# - xeno-config MUST be installed ($ sudo apt-get install libxenomai-dev)
# - Usage of "--skin" is now deprecated in Xenomai.
# - Remove this comment for older versions
#SKIN := xeno

RT_DIR ?= /usr/xenomai
RT_CONFIG := $(RT_DIR)/bin/xeno-config

ifneq ($(SKIN),)
RT_CFLAGS ?= $(shell $(RT_CONFIG) --$(SKIN)-cflags)
RT_LIB_DIR ?= $(shell $(RT_CONFIG) --library-dir) -Wl,-rpath $(shell $(RT_CONFIG) --library-dir)
else
SKIN := rtdm
RT_CFLAGS ?= $(shell $(RT_CONFIG) --skin $(SKIN) --cflags)
RT_LDFLAGS ?= -Wl,-rpath $(shell $(RT_CONFIG) --library-dir) $(shell $(RT_CONFIG) --skin $(SKIN) --ldflags)
endif
endif

ifeq ($(RT), RTAI)
SKIN := lxrt

RT_DIR ?= /usr/realtime

RT_CONFIG       ?= $(RT_DIR)/bin/rtai-config
RT_LIB_DIR      ?= $(shell $(RT_CONFIG) --library-dir) -Wl,-rpath $(shell $(RT_CONFIG) --library-dir)
RT_CFLAGS       ?= $(shell $(RT_CONFIG) --$(SKIN)-cflags)
endif

ifeq ($(HOSTTYPE),x86_64)
LIBPATH := /usr/lib64
else
LIBPATH := /usr/lib
endif
INCPATH := /usr/include

LDNAME := libpcan.so
SONAME := $(LDNAME).0
TARGET := $(SONAME).6

LDNAMEFD := libpcanfd.so
SONAMEFD := $(LDNAMEFD).0
TARGETFD := $(SONAMEFD).1

CFLAGS := -D$(RT) $(INC) -fPIC -shared -O2 -Wall $(RT_CFLAGS)
LDFLAGS := -lc $(RT_LDFLAGS)

all: $(TARGET) $(TARGETFD)

$(TARGET) : $(FILES)
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -Wl,-soname,$(SONAME) -o $@
	ln -sf $@ $(LDNAME)

ifneq ($(FILESFD),)
# libpcanfd.c includes libpcan.c
$(TARGETFD) : $(FILESFD) $(FILES)
	$(CC) $(FILESFD) $(CFLAGS) -DPCANFD_OLD_STYLE_API $(LDFLAGS) -Wl,-soname,$(SONAMEFD) -o $@
	ln -sf $@ $(LDNAMEFD)
endif

clean:
	rm -f $(SRC)/*~ $(SRC)/*.o *~ *.so.* *.so

# root access only
install:
	mkdir -p $(DESTDIR)$(LIBPATH)
	cp $(TARGET) $(DESTDIR)$(LIBPATH)
	ln -sf $(TARGET) $(DESTDIR)$(LIBPATH)/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(LIBPATH)/$(LDNAME)
	mkdir -p $(DESTDIR)$(INCPATH)
	cp libpcan.h $(DESTDIR)$(INCPATH)
	chmod 644 $(DESTDIR)$(INCPATH)/libpcan.h
ifneq ($(FILESFD),)
	cp $(TARGETFD) $(DESTDIR)$(LIBPATH)
	ln -sf $(TARGETFD) $(DESTDIR)$(LIBPATH)/$(SONAMEFD)
	ln -sf $(SONAMEFD) $(DESTDIR)$(LIBPATH)/$(LDNAMEFD)
	cp libpcanfd.h $(DESTDIR)$(INCPATH)
	chmod 644 $(DESTDIR)$(INCPATH)/libpcanfd.h
endif
ifeq ($(DESTDIR),)
	/sbin/ldconfig
endif

uninstall:
	-rm -f $(DESTDIR)$(LIBPATH)/$(TARGET) \
	       $(DESTDIR)$(LIBPATH)/$(SONAME) \
	       $(DESTDIR)$(LIBPATH)/$(LDNAME)
	-rm -f $(DESTDIR)$(INCPATH)/libpcan.h $(DESTDIR)$(INCPATH)/pcan.h
ifneq ($(FILESFD),)
	-rm -f $(DESTDIR)$(LIBPATH)/$(TARGETFD) \
	       $(DESTDIR)$(LIBPATH)/$(SONAMEFD) \
	       $(DESTDIR)$(LIBPATH)/$(LDNAMEFD)
	-rm -f $(DESTDIR)$(INCPATH)/libpcanfd.h
endif
ifeq ($(DESTDIR),)
	/sbin/ldconfig
endif

xeno:
	$(MAKE) RT=XENOMAI

rtai:
	$(MAKE) RT=RTAI
