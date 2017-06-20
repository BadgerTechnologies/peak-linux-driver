#****************************************************************************
# Copyright (C) 2001-2010  PEAK System-Technik GmbH
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
# Makefile - Makefile for receivetest and transmittest programs
#
# $Id: Makefile 1157 2016-03-08 13:57:22Z stephane $
#
#****************************************************************************

PCANDRV_DIR := ../driver
PCANLIB_DIR := ../lib

SRC := src
INC := -I. -I$(PCANLIB_DIR) -I$(PCANDRV_DIR)
RT ?= NO_RT

BINDIR = $(DESTDIR)/usr/local/bin

USERAPP_LIBS := -lpcanfd

ifeq ($(RT), XENOMAI)

# Xenomai directory, xeno-config and library directory
#RT_DIR := /usr
RT_DIR := /usr/xenomai
RT_CONFIG := $(RT_DIR)/bin/xeno-config

# User space application compiler options
# Note: --xeno-cflags is deprecated
# SKIN := xeno
ifneq ($(SKIN),)
USERAPP_CFLAGS ?= $(shell $(RT_CONFIG) --$(SKIN)-cflags)
RT_LIB_DIR ?= $(shell $(RT_CONFIG) --library-dir) -Wl,-rpath $(shell $(RT_CONFIG) --library-dir)
USERAPP_LDFLAGS ?= $(shell $(RT_CONFIG) --$(SKIN)-ldflags) -L$(RT_LIB_DIR)
else
SKIN = rtdm
RT_LIB_DIR := $(shell $(RT_CONFIG) --library-dir)
USERAPP_CFLAGS := $(shell $(RT_CONFIG) --skin=$(SKIN) --cflags)
USERAPP_LDFLAGS := -Wl,-rpath $(RT_LIB_DIR) $(shell $(RT_CONFIG) --skin=$(SKIN) --ldflags)
endif

USERAPP_LIBS := -lnative $(USERAPP_LIBS)
endif

ifeq ($(RT), RTAI)
SKIN = lxrt

# Rtai directory, rtai-config and library directory
RT_DIR := /usr/realtime
RT_CONFIG := $(RT_DIR)/bin/rtai-config

# User space application compiler options
USERAPP_CFLAGS := $(shell $(RT_CONFIG) --$(SKIN)-cflags)
USERAPP_LDFLAGS := $(shell $(RT_CONFIG) --$(SKIN)-ldflags)
endif

LDLIBS := -L$(PCANLIB_DIR)

ifeq ($(HOSTTYPE),x86_64)
LDLIBS := $(LDLIBS) -L/lib64 -L/usr/lib64 -L/usr/local/lib64
else
LDLIBS := $(LDLIBS) -L/lib -L/usr/lib -L/usr/local/lib
endif

ifneq ($(RT), NO_RT)
DBGFLAGS :=
else
DBGFLAGS := -g
endif

CFLAGS := -D$(RT) $(DBGFLAGS) $(INC) $(USERAPP_CFLAGS)
LDFLAGS := $(USERAPP_LDFLAGS) $(USERAPP_LIBS) $(LDLIBS)

# take different source files for realtime and 'normal' test programms
ifeq ($(RT), NO_RT)
TARGET1 := receivetest
FILES1 := $(SRC)/$(TARGET1).c $(SRC)/common.c

TARGET2 := transmitest
FILES2 := $(SRC)/$(TARGET2).cpp $(SRC)/common.c $(SRC)/parser.cpp
else
TARGET1 := receivetest
FILES1 := $(SRC)/$(TARGET1)_rt.c $(SRC)/common.c

TARGET2 := transmitest
FILES2 := $(SRC)/$(TARGET2)_rt.cpp $(SRC)/common.c $(SRC)/parser.cpp
endif

TARGET3 := bitratetest
FILES3 := $(SRC)/$(TARGET3).c $(SRC)/common.c

TARGET4 := filtertest
FILES4 := $(SRC)/$(TARGET4).cpp $(SRC)/common.c

TARGET5 := pcan-settings
FILES5 := $(SRC)/$(TARGET5).c

TARGET6 := pcanfdtst
FILES6 := $(SRC)/$(TARGET6).c

ALL = $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) $(TARGET5) $(TARGET6)

all: $(ALL)

$(TARGET1): $(FILES1)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(TARGET2): $(FILES2)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -lstdc++ -o $@
	
$(TARGET3): $(FILES3)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(TARGET4): $(FILES4)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -lstdc++ -o $@

$(TARGET5): $(FILES5)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -lpopt -g -o $@

$(TARGET6): $(FILES6)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	-rm -f $(SRC)/*~ $(SRC)/*.o *~ $(ALL)
	
install:
	cp $(ALL) $(BINDIR)

uninstall:
	-cd $(BINDIR); rm -f $(ALL)

xeno:
	$(MAKE) RT=XENOMAI

rtai:
	$(MAKE) RT=RTAI
