include $(ROOTDIR)/config.arch

BOGUS_TARGETS		:= FORCE makefile $(ROOTDIR)/config.arch

CC			:= $(CROSS_COMPILE)gcc
STRIP			:= $(CROSS_COMPILE)strip

CFLAGS			:= -include locale.h

export CC CFLAGS

#
# Settings that are specific to this package
#

CPPFLAGS		:= -I$(ROOTDIR)/lib/ncurses/include
LDFLAGS			:= -L$(ROOTDIR)/lib/ncurses/lib -lncurses

.PHONY: all
all::
	if [ ! -e Makefile ]; then \
		./configure --host=$(MACHINE)-linux CFLAGS="$(CFLAGS)" CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" \
			|| exit 1 ; \
	fi
	$(MAKE) -f Makefile SUBDIRS="fdisk"

.PHONY: clean distclean
clean distclean::
	[ ! -f Makefile ] || $(MAKE) -f Makefile $@

.PHONY: romfs
romfs::
	$(STRIP) fdisk/fdisk fdisk/sfdisk fdisk/cfdisk
	$(ROMFSINST) -e CONFIG_USER_FDISK_FDISK fdisk/fdisk \
		/bin/fdisk
	$(ROMFSINST) -e CONFIG_USER_FDISK_FDISK fdisk/sfdisk \
		/bin/sfdisk
	$(ROMFSINST) -e CONFIG_USER_FDISK_FDISK fdisk/cfdisk \
		/bin/cfdisk

# These targets must not be passed through to the original Makefile
.PHONY: $(BOGUS_TARGETS)
$(BOGUS_TARGETS)::

# Everything else (maybe including clean, distclean) does get passed through
%:: FORCE
	$(MAKE) -f Makefile $@
