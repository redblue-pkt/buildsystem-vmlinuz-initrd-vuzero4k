USRPGMS = aread awrite ttcp_atm
PGMS = align aping br bw isp window
bin_PROGRAMS = $(USRPGMS)
noinst_PROGRAMS = $(PGMS)

LDADD = $(top_builddir)/src/lib/libatm.la

aread_SOURCES = aread.c
awrite_SOURCES = awrite.c
ttcp_atm_SOURCES = ttcp.c

align_SOURCES = align.c
aping_SOURCES = aping.c
br_SOURCES = br.c
bw_SOURCES = bw.c
isp_SOURCES = isp.c isp.h ispl_y.y ispl_l.l
isp_XTRAS = $(LDADD)
isp_LDADD = $(isp_XTRAS) -lfl
isp_DEPENDENCIES = $(isp_XTRAS) errnos.inc
window_SOURCES = window.c

CLEANFILES = errnos.inc

EXTRA_DIST = ispl_y.h README.isp mkerrnos.pl

isp.o: errnos.inc

errnos.inc: mkerrnos.pl
	@PERL@ ./mkerrnos.pl </usr/include/asm/errno.h \
		>errnos.inc || { rm -f errnos.inc; exit 1; }

