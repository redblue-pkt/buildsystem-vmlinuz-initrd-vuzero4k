#
# General settings
#

include $(ROOTDIR)/config.arch

BOGUS_TARGETS		:= FORCE makefile $(ROOTDIR)/config.arch

STRIP			:= $(CROSS_COMPILE)strip
CC			:= $(CROSS_COMPILE)gcc
CXX			:= $(CROSS_COMPILE)g++
CFLAGS			:= -Os

export CC CFLAGS

#
# Settings that are specific to this package
#

CFLAGS			+= -D_FILE_OFFSET_BITS=64
CFLAGS			+= -I$(ROOTDIR)/user/e2fsprogs/lib
CFLAGS			+= -I$(ROOTDIR)/lib/popt
CXXFLAGS		:= $(CFLAGS)

LDFLAGS			:= -L$(ROOTDIR)/user/e2fsprogs/lib
LDFLAGS			+= -L$(ROOTDIR)/lib/popt/.libs

export CFLAGS CXXFLAGS LDFLAGS

#
# Build targets
#

.PHONY: all
all::
	$(MAKE) -f Makefile.brcmstb CC=$(CC) CXX=$(CXX)

.PHONY: romfs
romfs::
	$(STRIP) gdisk sgdisk
	$(ROMFSINST) -e CONFIG_USER_GPTFDISK_GDISK  /bin/gdisk
	$(ROMFSINST) -e CONFIG_USER_GPTFDISK_SGDISK /bin/sgdisk

.PHONY: clean distclean
clean distclean::
	$(MAKE) -f Makefile.brcmstb clean

# These targets must not be passed through to the original Makefile
.PHONY: $(BOGUS_TARGETS)
$(BOGUS_TARGETS)::

# Everything else (maybe including clean, distclean) does get passed through
%:: FORCE
	$(MAKE) -f Makefile.brcmstb $@
