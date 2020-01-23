/*
 * Copyright (c) 1983, 1991, 1993, 1994, 2002
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
static char sccsid[] = "@(#)inetd.c	8.4 (Berkeley) 4/13/94";
#endif

/*
 * Inetd - Internet super-server
 *
 * This program invokes all internet services as needed.  Connection-oriented
 * services are invoked each time a connection is made, by creating a process.
 * This process is passed the connection as file descriptor 0 and is expected
 * to do a getpeername to find out the source host and port.
 *
 * Datagram oriented services are invoked when a datagram
 * arrives; a process is created and passed a pending message
 * on file descriptor 0.  Datagram servers may either connect
 * to their peer, freeing up the original socket for inetd
 * to receive further messages on, or ``take over the socket'',
 * processing all arriving datagrams and, eventually, timing
 * out.	 The first type of server is said to be ``multi-threaded'';
 * the second type of server ``single-threaded''.
 *
 * Inetd uses a configuration file which is read at startup
 * and, possibly, at some later time in response to a hangup signal.
 * The configuration file is ``free format'' with fields given in the
 * order shown below.  Continuation lines for an entry must being with
 * a space or tab.  All fields must be present in each entry.
 *
 *	service name			must be in /etc/services or must
 *					name a tcpmux service
 *	socket type			stream/dgram/raw/rdm/seqpacket
 *	protocol			must be in /etc/protocols
 *	wait/nowait			single-threaded/multi-threaded
 *	user				user to run daemon as
 *	server program			full path name
 *	server program arguments	maximum of MAXARGS (20)
 *
 * TCP services without official port numbers are handled with the
 * RFC1078-based tcpmux internal service. Tcpmux listens on port 1 for
 * requests. When a connection is made from a foreign host, the service
 * requested is passed to tcpmux, which looks it up in the servtab list
 * and returns the proper entry for the service. Tcpmux returns a
 * negative reply if the service doesn't exist, otherwise the invoked
 * server is expected to return the positive reply if the service type in
 * inetd.conf file has the prefix "tcpmux/". If the service type has the
 * prefix "tcpmux/+", tcpmux will return the positive reply for the
 * process; this is for compatibility with older server code, and also
 * allows you to invoke programs that use stdin/stdout without putting any
 * special server code in them. Services that use tcpmux are "nowait"
 * because they do not have a well-known port and hence cannot listen
 * for new requests.
 *
 * Comment lines are indicated by a `#' in column 1.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <getopt.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <grp.h>

#define	TOOMANY		40		/* don't start more than TOOMANY */
#define	CNT_INTVL	60		/* servers in CNT_INTVL sec. */
#define	RETRYTIME	(60*10)		/* retry after bind or server fail */

#ifndef SIGCHLD
#define SIGCHLD	SIGCLD
#endif
#define	SIGBLOCK	(sigmask(SIGCHLD)|sigmask(SIGHUP)|sigmask(SIGALRM))

extern char *__progname;

int	debug = 0;
int	nsock, maxsock;
fd_set	allsock;
int	options;
int	timingout;
int	toomany = TOOMANY;
struct	servent *sp;

struct	servtab {
  char	*se_service;		/* name of service */
  int	se_socktype;		/* type of socket to use */
  char	*se_proto;		/* protocol used */
  short	se_wait;		/* single threaded server */
  short	se_checked;		/* looked at during merge */
  char	*se_user;		/* user name to run as */
  struct biltin *se_bi;		/* if built-in, description */
  char	*se_server;		/* server program */
#define	MAXARGV 20
  char	*se_argv[MAXARGV+1];	/* program arguments */
  int	se_fd;			/* open descriptor */
  int	se_type;		/* type */
  struct sockaddr_in se_ctrladdr;/* bound address */
  int	se_count;		/* number started since se_time */
  struct	timeval se_time;	/* start of se_count */
  struct	servtab *se_next;
} *servtab;

#define NORM_TYPE	0
#define MUX_TYPE	1
#define MUXPLUS_TYPE	2
#define ISMUX(sep)	(((sep)->se_type == MUX_TYPE) || \
			 ((sep)->se_type == MUXPLUS_TYPE))
#define ISMUXPLUS(sep)	((sep)->se_type == MUXPLUS_TYPE)


void		chargen_dg __P((int, struct servtab *));
void		chargen_stream __P((int, struct servtab *));
void		close_sep __P((struct servtab *));
void		config __P((int));
void		daytime_dg __P((int, struct servtab *));
void		daytime_stream __P((int, struct servtab *));
void		discard_dg __P((int, struct servtab *));
void		discard_stream __P((int, struct servtab *));
void		echo_dg __P((int, struct servtab *));
void		echo_stream __P((int, struct servtab *));
void		endconfig __P((FILE *));
struct servtab *enter __P((struct servtab *));
void		freeconfig __P((struct servtab *));
struct servtab *getconfigent __P((FILE *, const char *));
void		machtime_dg __P((int, struct servtab *));
void		machtime_stream __P((int, struct servtab *));
char	       *newstr __P((const char *));
char	       *nextline __P((FILE *));
void		nextconfig __P((const char *));
void		print_service __P((const char *, const char *, struct servtab *));
void		reapchild __P((int));
void		retry __P((int));
FILE           *setconfig __P((const char *));
void		setup __P((struct servtab *));
char	       *sskip __P((char **, FILE *, const char *));
char	       *skip __P((char **, FILE *));
void		tcpmux __P((int s, struct servtab *sep));
void		set_proc_title __P ((char *, int));
void		initring __P((void));
long		machtime __P((void));
void            run_service __P ((int ctrl, struct servtab *sep));

struct biltin {
  const char	*bi_service;	/* internally provided service name */
  int	bi_socktype;		/* type of socket supported */
  short	bi_fork;		/* 1 if should fork before call */
  short	bi_wait;		/* 1 if should wait for child */
  void	(*bi_fn) __P((int s, struct servtab *)); /*function which performs it */
} biltins[] = {
  /* Echo received data */
  { "echo",	SOCK_STREAM,	1, 0,	echo_stream },
  { "echo",	SOCK_DGRAM,	0, 0,	echo_dg },

  /* Internet /dev/null */
  { "discard",	SOCK_STREAM,	1, 0,	discard_stream },
  { "discard",	SOCK_DGRAM,	0, 0,	discard_dg },

  /* Return 32 bit time since 1970 */
  { "time",	SOCK_STREAM,	0, 0,	machtime_stream },
  { "time",	SOCK_DGRAM,	0, 0,	machtime_dg },

  /* Return human-readable time */
  { "daytime",	SOCK_STREAM,	0, 0,	daytime_stream },
  { "daytime",	SOCK_DGRAM,	0, 0,	daytime_dg },

  /* Familiar character generator */
  { "chargen",	SOCK_STREAM,	1, 0,	chargen_stream },
  { "chargen",	SOCK_DGRAM,	0, 0,	chargen_dg },

  { "tcpmux",	SOCK_STREAM,	1, 0,	tcpmux },

  { NULL, 0, 0, 0, NULL }
};

#define NUMINT	(sizeof(intab) / sizeof(struct inent))
char	**Argv;
char 	*LastArg;

char **config_files;

static void
usage (int err)
{
  if (err != 0)
    {
      fprintf (stderr, "Usage: %s [OPTION...] [CONF-FILE [CONF-DIR]] ...\n",
	       __progname);
      fprintf (stderr, "Try `%s --help' for more information.\n", __progname);
    }
  else
    {
      fprintf (stdout, "Usage: %s [OPTION...] [CONF-FILE [CONF-DIR]] ...\n",
	       __progname);
      puts ("Internet super-server.\n\n\
  -d, --debug               Debug mode\n\
  -R, --rate NUMBER         Maximum invocation rate (per second)\n\
      --help                Display this help and exit\n\
  -V, --version             Output version information and exit");

      fprintf (stdout, "\nSubmit bug reports to %s.\n", PACKAGE_BUGREPORT);
    }
  exit (err);
}

static const char *short_options = "dR:V";
static struct option long_options[] =
{
  { "debug", no_argument, 0, 'd' },
  { "rate", required_argument, 0, 'R' },
  { "help", no_argument, 0, '&' },
  { "version", no_argument, 0, 'V' },
  { 0, 0, 0, 0 }
};

int
main (int argc, char *argv[], char *envp[])
{
  int option;
  struct servtab *sep;
  int dofork;
  pid_t pid;

#ifndef HAVE___PROGNAME
  __progname = argv[0];
#endif

  Argv = argv;
  if (envp == 0 || *envp == 0)
    envp = argv;
  while (*envp)
    envp++;
  LastArg = envp[-1] + strlen (envp[-1]);

  while ((option = getopt_long (argc, argv, short_options,
				long_options, 0)) != EOF)
    {
      switch (option)
	{
	case 'd': /* Debug.  */
	  debug = 1;
	  options |= SO_DEBUG;
	  break;

	case 'R': /* Invocation rate.  */
	  {
	    char *p;
	    int number;
	    number = strtol (optarg, &p, 0);
	    if (number < 1 || *p)
	      syslog (LOG_ERR,
		      "-R %s: bad value for service invocation rate",
		      optarg);
	    else
	      toomany = number;
	    break;
	  }

	case '&': /* Usage.  */
	  usage (0);
	  /* Not reached.  */

	case 'V': /* Version.  */
	  printf ("inetd (%s) %s\n", PACKAGE_NAME, PACKAGE_VERSION);
	  exit (0);
	  /* Not reached.  */

	case '?':
	default:
	  usage (1);
	  /* Not reached.  */
	}
    }

  if (optind < argc)
    {
      int i;
      config_files = calloc (argc - optind + 1, sizeof (*config_files));
      for (i = 0; optind < argc; optind++, i++)
	{
	  config_files[i] = strdup (argv[optind]);
	}
    }
  else
    {
      config_files = calloc (3, sizeof (*config_files));
      config_files[0] = newstr (PATH_INETDCONF);
      config_files[1] = newstr (PATH_INETDDIR);
    }

  if (debug == 0)
    {
      daemon (0, 0);
    }

  openlog ("inetd", LOG_PID | LOG_NOWAIT, LOG_DAEMON);

#if defined(HAVE_SIGACTION)
  {
    struct sigaction sa;
    memset ((char *)&sa, 0, sizeof(sa));
    sigemptyset (&sa.sa_mask);
    sigaddset (&sa.sa_mask, SIGCHLD);
    sigaddset (&sa.sa_mask, SIGHUP);
    sigaddset (&sa.sa_mask, SIGALRM);
#ifdef SA_RESTART
    sa.sa_flags = SA_RESTART;
#endif
    sa.sa_handler = retry;
    sigaction (SIGALRM, &sa, (struct sigaction *)0);
    config (0);
    sa.sa_handler = config;
    sigaction (SIGHUP, &sa, (struct sigaction *)0);
    sa.sa_handler = reapchild;
    sigaction (SIGCHLD, &sa, (struct sigaction *)0);
    sa.sa_handler = SIG_IGN;
    sigaction (SIGPIPE, &sa, (struct sigaction *)0);
  }
#elif defined(HAVE_SIGVEC)
  {
    struct sigvec sv;
    memset (&sv, 0, sizeof(sv));
    sv.sv_mask = SIGBLOCK;
    sv.sv_handler = retry;
    sigvec (SIGALRM, &sv, (struct sigvec *)0);
    config (0);
    sv.sv_handler = config;
    sigvec (SIGHUP, &sv, (struct sigvec *)0);
    sv.sv_handler = reapchild;
    sigvec (SIGCHLD, &sv, (struct sigvec *)0);
    sv.sv_mask = 0L;
    sv.sv_handler = SIG_IGN;
    sigvec (SIGPIPE, &sv, (struct sigvec *)0);
  }
#else /* !HAVE_SIGVEC */
  signal (SIGALRM, retry);
  config (0);
  signal (SIGHUP, config);
  signal (SIGCHLD, reapchild);
  signal (SIGPIPE, SIG_IGN);
#endif /* HAVE_SIGACTION */

  {
    /* space for daemons to overwrite environment for ps */
#define	DUMMYSIZE	100
    char dummy[DUMMYSIZE];

    (void)memset(dummy, 'x', DUMMYSIZE - 1);
    dummy[DUMMYSIZE - 1] = '\0';
    (void)setenv("inetd_dummy", dummy, 1);
  }

  for (;;)
    {
      int n, ctrl;
      fd_set readable;

      if (nsock == 0)
	{
#ifdef HAVE_SIGACTION
	  {
	    sigset_t sigs;
	    sigemptyset (&sigs);
	    sigaddset (&sigs, SIGCHLD);
	    sigaddset (&sigs, SIGHUP);
	    sigaddset (&sigs, SIGALRM);
	    sigprocmask (SIG_BLOCK, &sigs, 0);
	  }
#else
	  (void) sigblock (SIGBLOCK);
#endif
	  while (nsock == 0)
	    sigpause (0L);
#ifdef HAVE_SIGACTION
	  {
	    sigset_t empty;
	    sigemptyset (&empty);
	    sigprocmask (SIG_SETMASK, &empty, 0);
	  }
#else
	  (void) sigsetmask (0L);
#endif
	}
      readable = allsock;
      if ((n = select (maxsock + 1, &readable, (fd_set *)0,
		      (fd_set *)0, (struct timeval *)0)) <= 0)
	{
	  if (n < 0 && errno != EINTR)
	    syslog (LOG_WARNING, "select: %m");
	  sleep (1);
	  continue;
	}
      for (sep = servtab; n && sep; sep = sep->se_next)
	if (sep->se_fd != -1 && FD_ISSET (sep->se_fd, &readable))
	  {
	    n--;
	    if (debug)
	      fprintf (stderr, "someone wants %s\n", sep->se_service);
	    if (!sep->se_wait && sep->se_socktype == SOCK_STREAM)
	      {
		ctrl = accept (sep->se_fd, (struct sockaddr *)0, (int *)0);
		if (debug)
		  fprintf (stderr, "accept, ctrl %d\n", ctrl);
		if (ctrl < 0)
		  {
		    if (errno != EINTR)
		      syslog (LOG_WARNING, "accept (for %s): %m",
			      sep->se_service);
		    continue;
		  }
	      }
	    else
	      ctrl = sep->se_fd;
#ifdef HAVE_SIGACTION
	    {
	      sigset_t sigs;
	      sigemptyset (&sigs);
	      sigaddset (&sigs, SIGCHLD);
	      sigaddset (&sigs, SIGHUP);
	      sigaddset (&sigs, SIGALRM);
	      sigprocmask (SIG_BLOCK, &sigs, 0);
	    }
#else
	    (void) sigblock (SIGBLOCK);
#endif
	    pid = 0;
	    dofork = (sep->se_bi == 0 || sep->se_bi->bi_fork);
	    if (dofork)
	      {
		if (sep->se_count++ == 0)
		  (void)gettimeofday (&sep->se_time, (struct timezone *)0);
		else if (sep->se_count >= toomany)
		  {
		    struct timeval now;

		    (void)gettimeofday (&now, (struct timezone *)0);
		    if (now.tv_sec - sep->se_time.tv_sec > CNT_INTVL)
		      {
			sep->se_time = now;
			sep->se_count = 1;
		      }
		    else
		      {
			syslog (LOG_ERR,
				"%s/%s server failing (looping), service terminated",
				sep->se_service, sep->se_proto);
			close_sep (sep);
#ifdef HAVE_SIGACTION
			{
			  sigset_t empty;
			  sigemptyset (&empty);
			  sigprocmask (SIG_SETMASK, &empty, 0);
			}
#else
			sigsetmask (0L);
#endif
			if (!timingout)
			  {
			    timingout = 1;
			    alarm (RETRYTIME);
			  }
			continue;
		      }
		  }
		pid = fork ();
	      }
	    if (pid < 0)
	      {
		syslog (LOG_ERR, "fork: %m");
		if (!sep->se_wait && sep->se_socktype == SOCK_STREAM)
		  close (ctrl);
#ifdef HAVE_SIGACTION
		{
		  sigset_t empty;
		  sigemptyset (&empty);
		  sigprocmask (SIG_SETMASK, &empty, 0);
		}
#else
		sigsetmask (0L);
#endif
		sleep (1);
		continue;
	      }
	    if (pid && sep->se_wait)
	      {
		sep->se_wait = pid;
		if (sep->se_fd >= 0)
		  {
		    FD_CLR (sep->se_fd, &allsock);
		    nsock--;
		  }
	      }
#ifdef HAVE_SIGACTION
	    {
	      sigset_t empty;
	      sigemptyset (&empty);
	      sigprocmask (SIG_SETMASK, &empty, 0);
	    }
#else
	    sigsetmask (0L);
#endif
	    if (pid == 0)
	      {
		if (debug && dofork)
		  setsid ();
		if (dofork)
		  {
		    int sock;
		    if (debug)
		      fprintf (stderr, "+ Closing from %d\n", maxsock);
		    for (sock = maxsock; sock > 2; sock--)
		      if (sock != ctrl)
			close (sock);
		  }
		run_service (ctrl, sep);
	      }
	    if (!sep->se_wait && sep->se_socktype == SOCK_STREAM)
	      close (ctrl);
	  }
    }
}

void
run_service (int ctrl, struct servtab *sep)
{
  struct passwd *pwd;
  char buf[50];

  if (sep->se_bi)
    {
      (*sep->se_bi->bi_fn) (ctrl, sep);
    }
  else
    {
      if (debug)
	fprintf (stderr, "%d execl %s\n", getpid (), sep->se_server);
      dup2 (ctrl, 0);
      close (ctrl);
      dup2 (0, 1);
      dup2 (0, 2);
      if ((pwd = getpwnam (sep->se_user)) == NULL)
	{
	  syslog (LOG_ERR, "%s/%s: %s: No such user",
		  sep->se_service, sep->se_proto, sep->se_user);
	  if (sep->se_socktype != SOCK_STREAM)
	    recv (0, buf, sizeof buf, 0);
	  _exit (1);
	}
      if (pwd->pw_uid)
	{
	  if (setgid (pwd->pw_gid) < 0)
	    {
	      syslog (LOG_ERR, "%s: can't set gid %d: %m",
		      sep->se_service, pwd->pw_gid);
	      _exit (1);
	    }
#ifdef HAVE_INITGROUPS
	  (void) initgroups (pwd->pw_name, pwd->pw_gid);
#endif
	  if (setuid (pwd->pw_uid) < 0)
	    {
	      syslog (LOG_ERR, "%s: can't set uid %d: %m",
		      sep->se_service, pwd->pw_uid);
	      _exit (1);
	    }
	}
      execv (sep->se_server, sep->se_argv);
      if (sep->se_socktype != SOCK_STREAM)
	recv (0, buf, sizeof buf, 0);
      syslog (LOG_ERR, "cannot execute %s: %m", sep->se_server);
      _exit (1);
    }
}

void
reapchild (int signo)
{
  int status;
  pid_t pid;
  struct servtab *sep;

  (void)signo; /* shutup gcc */
  for (;;)
    {
#ifdef HAVE_WAIT3
      pid = wait3 (&status, WNOHANG, (struct rusage *)0);
#else
      pid = wait (&status);
#endif
      if (pid <= 0)
	break;
      if (debug)
	fprintf (stderr, "%d reaped, status %#x\n", pid, status);
      for (sep = servtab; sep; sep = sep->se_next)
	if (sep->se_wait == pid)
	  {
	    if (status)
	      syslog (LOG_WARNING, "%s: exit status 0x%x",
		      sep->se_server, status);
	    if (debug)
	      fprintf (stderr, "restored %s, fd %d\n",
		       sep->se_service, sep->se_fd);
	    FD_SET (sep->se_fd, &allsock);
	    nsock++;
	    sep->se_wait = 1;
	  }
    }
}

void
config (int signo)
{
  int i;
  struct stat stats;
  struct servtab *sep;

  (void)signo; /* Shutup gcc.  */

  for (sep = servtab; sep; sep = sep->se_next)
    sep->se_checked = 0;

  for (i = 0; config_files[i]; i++)
    {
      struct stat statbuf;

      if (stat (config_files[i], &statbuf) == 0)
	{
	  if (S_ISDIR (statbuf.st_mode))
	    {
	      DIR *dirp = opendir (config_files[i]);

	      if (dirp)
		{
		  struct dirent *dp;

		  while ((dp = readdir (dirp)) != NULL)
		    {
		      char *path = calloc (strlen (config_files[i])
					   + strlen (dp->d_name) + 2, 1);
		      if (path)
			{
			  sprintf (path,"%s/%s", config_files[i], dp->d_name);
			  if (stat (path, &stats) == 0
			      && S_ISREG(stats.st_mode))
			    {
			      nextconfig (path);
			    }
			  free (path);
			}
		    }
		  closedir (dirp);
		}
	    }
	  else if (S_ISREG (statbuf.st_mode))
	    {
	      nextconfig (config_files[i]);
	    }
	}
      else
	{
	  if (signo == 0)
	    fprintf (stderr, "inetd: %s, %s\n",
		     config_files[i], strerror(errno));
	  else
	    syslog (LOG_ERR, "%s: %m", config_files[i]);
	}
    }
}

void
nextconfig (const char *file)
{
  struct servtab *sep, *cp, **sepp;
  struct passwd *pwd;
  FILE * fconfig;
#ifdef HAVE_SIGACTION
  sigset_t sigs, osigs;
#else
  long omask;
#endif

  fconfig = setconfig (file);
  if (!fconfig)
    {
      syslog (LOG_ERR, "%s: %m", file);
      return;
    }
  while ((cp = getconfigent (fconfig, file)))
    {
      if ((pwd = getpwnam (cp->se_user)) == NULL)
	{
	  syslog(LOG_ERR, "%s/%s: No such user '%s', service ignored",
		 cp->se_service, cp->se_proto, cp->se_user);
	  continue;
	}
      /* Checking/Removing duplicates */
      for (sep = servtab; sep; sep = sep->se_next)
	if (strcmp (sep->se_service, cp->se_service) == 0
	    && strcmp (sep->se_proto, cp->se_proto) == 0
	    && ISMUX(sep) == ISMUX (cp))
	  break;
      if (sep != 0)
	{
	  int i;

#ifdef HAVE_SIGACTION
	  sigemptyset (&sigs);
	  sigaddset (&sigs, SIGCHLD);
	  sigaddset (&sigs, SIGHUP);
	  sigaddset (&sigs, SIGALRM);
	  sigprocmask (SIG_BLOCK, &sigs, &osigs);
#else
	  omask = sigblock (SIGBLOCK);
#endif
	  /*
	   * sep->se_wait may be holding the pid of a daemon
	   * that we're waiting for.  If so, don't overwrite
	   * it unless the config file explicitly says don't
	   * wait.
	   */
	  if (cp->se_bi == 0
	      && (sep->se_wait == 1 || cp->se_wait == 0))
	    sep->se_wait = cp->se_wait;
#define SWAP(a, b) { char *c = a; a = b; b = c; }
	  if (cp->se_user)
	    SWAP(sep->se_user, cp->se_user);
	  if (cp->se_server)
	    SWAP(sep->se_server, cp->se_server);
	  for (i = 0; i < MAXARGV; i++)
	    SWAP(sep->se_argv[i], cp->se_argv[i]);
#ifdef HAVE_SIGACTION
	  sigprocmask (SIG_SETMASK, &osigs, 0);
#else
	  sigsetmask (omask);
#endif
	  freeconfig (cp);
	  if (debug)
	    print_service (file, "REDO", sep);
	}
      else
	{
	  sep = enter (cp);
	  if (debug)
	    print_service (file, "ADD ", sep);
	}
      sep->se_checked = 1;
      if (ISMUX (sep))
	{
	  sep->se_fd = -1;
	  continue;
	}
      sp = getservbyname (sep->se_service, sep->se_proto);
      if (sp == 0)
	{
	  syslog (LOG_ERR, "%s/%s: unknown service",
		  sep->se_service, sep->se_proto);
	  sep->se_checked = 0;
	  continue;
	}
      if (sp->s_port != sep->se_ctrladdr.sin_port)
	{
	  sep->se_ctrladdr.sin_family = AF_INET;
	  sep->se_ctrladdr.sin_port = sp->s_port;
	  if (sep->se_fd >= 0)
	    close_sep (sep);
	}
      if (sep->se_fd == -1)
	setup (sep);
    }
  endconfig (fconfig);
  /*
   * Purge anything not looked at above.
   */
#ifdef HAVE_SIGACTION
  sigemptyset (&sigs);
  sigaddset (&sigs, SIGCHLD);
  sigaddset (&sigs, SIGHUP);
  sigaddset (&sigs, SIGALRM);
  sigprocmask (SIG_BLOCK, &sigs, &osigs);
#else
  omask = sigblock (SIGBLOCK);
#endif
  sepp = &servtab;
  while ((sep = *sepp))
    {
      if (sep->se_checked)
	{
	  sepp = &sep->se_next;
	  continue;
	}
      *sepp = sep->se_next;
      if (sep->se_fd >= 0)
	close_sep (sep);
      if (debug)
	print_service (file, "FREE", sep);
      freeconfig (sep);
      free ((char *)sep);
    }
#ifdef HAVE_SIGACTION
  sigprocmask (SIG_SETMASK, &osigs, 0);
#else
  (void) sigsetmask (omask);
#endif
}

void
retry (int signo)
{
  struct servtab *sep;

  (void)signo; /* shutup gcc */
  timingout = 0;
  for (sep = servtab; sep; sep = sep->se_next)
    if (sep->se_fd == -1 && !ISMUX (sep))
      setup (sep);
}

void
setup (struct servtab *sep)
{
  int on = 1;

  if ((sep->se_fd = socket (AF_INET, sep->se_socktype, 0)) < 0)
    {
      if (debug)
	fprintf (stderr, "socket failed on %s/%s: %s\n",
		 sep->se_service, sep->se_proto, strerror (errno));
      syslog(LOG_ERR, "%s/%s: socket: %m", sep->se_service, sep->se_proto);
      return;
    }
#define	turnon(fd, opt) \
setsockopt(fd, SOL_SOCKET, opt, (char *)&on, sizeof (on))
  if (strcmp (sep->se_proto, "tcp") == 0 && (options & SO_DEBUG)
      && turnon(sep->se_fd, SO_DEBUG) < 0)
    syslog (LOG_ERR, "setsockopt (SO_DEBUG): %m");
  if (turnon (sep->se_fd, SO_REUSEADDR) < 0)
    syslog (LOG_ERR, "setsockopt (SO_REUSEADDR): %m");
#undef turnon
  if (bind (sep->se_fd, (struct sockaddr *)&sep->se_ctrladdr,
	    sizeof (sep->se_ctrladdr)) < 0)
    {
      if (debug)
	fprintf (stderr, "bind failed on %s/%s: %s\n",
		 sep->se_service, sep->se_proto, strerror (errno));
      syslog(LOG_ERR, "%s/%s: bind: %m", sep->se_service, sep->se_proto);
      (void) close (sep->se_fd);
      sep->se_fd = -1;
      if (!timingout)
	{
	  timingout = 1;
	  alarm (RETRYTIME);
	}
      return;
    }
  if (sep->se_socktype == SOCK_STREAM)
    listen (sep->se_fd, 10);
  FD_SET (sep->se_fd, &allsock);
  nsock++;
  if (sep->se_fd > maxsock)
    maxsock = sep->se_fd;
  if (debug)
    {
      fprintf(stderr, "registered %s on %d\n", sep->se_server, sep->se_fd);
    }
}

/*
 * Finish with a service and its socket.
 */
void
close_sep (struct servtab *sep)
{
  if (sep->se_fd >= 0)
    {
      nsock--;
      FD_CLR (sep->se_fd, &allsock);
      (void) close (sep->se_fd);
      sep->se_fd = -1;
    }
  sep->se_count = 0;
  /*
   * Don't keep the pid of this running deamon: when reapchild()
   * reaps this pid, it would erroneously increment nsock.
   */
  if (sep->se_wait > 1)
    sep->se_wait = 1;
}

struct servtab *
enter (struct servtab *cp)
{
  struct servtab *sep;
#ifdef HAVE_SIGACTION
  sigset_t sigs, osigs;
#else
  long omask;
#endif

  sep = (struct servtab *)malloc (sizeof (*sep));
  if (sep == (struct servtab *)0)
    {
      syslog (LOG_ERR, "Out of memory.");
      exit (-1);
    }
  *sep = *cp;
  sep->se_fd = -1;
#ifdef HAVE_SIGACTION
  sigemptyset (&sigs);
  sigaddset (&sigs, SIGCHLD);
  sigaddset (&sigs, SIGHUP);
  sigaddset (&sigs, SIGALRM);
  sigprocmask (SIG_BLOCK, &sigs, &osigs);
#else
  omask = sigblock (SIGBLOCK);
#endif
  sep->se_next = servtab;
  servtab = sep;
#ifdef HAVE_SIGACTION
  sigprocmask (SIG_SETMASK, &osigs, 0);
#else
  sigsetmask (omask);
#endif
  return sep;
}

struct	servtab serv;
#ifdef LINE_MAX
char	line[LINE_MAX];
#else
char 	line[2048];
#endif

FILE *
setconfig (const char *file)
{
  return fopen (file, "r");
}

void
endconfig (FILE *fconfig)
{
  if (fconfig)
    {
      (void) fclose (fconfig);
    }
}

struct servtab *
getconfigent (FILE *fconfig, const char *file)
{
  struct servtab *sep = &serv;
  int argc;
  char *cp, *arg;
  static char TCPMUX_TOKEN[] = "tcpmux/";
#define MUX_LEN		(sizeof(TCPMUX_TOKEN)-1)

more:
  while ((cp = nextline (fconfig)) && (*cp == '#' || *cp == '\0'))
    ;
  if (cp == NULL)
    return ((struct servtab *)0);
  /*
   * clear the static buffer, since some fields (se_ctrladdr,
   * for example) don't get initialized here.
   */
  memset ((caddr_t)sep, 0, sizeof *sep);
  arg = skip (&cp, fconfig);
  if (cp == NULL)
    {
      /* got an empty line containing just blanks/tabs. */
      goto more;
    }
  if (strncmp (arg, TCPMUX_TOKEN, MUX_LEN) == 0)
    {
      char *c = arg + MUX_LEN;
      if (*c == '+')
	{
	  sep->se_type = MUXPLUS_TYPE;
	  c++;
	}
      else
	sep->se_type = MUX_TYPE;
      sep->se_service = newstr (c);
    }
  else
    {
      sep->se_service = newstr (arg);
      sep->se_type = NORM_TYPE;
    }
  arg = sskip (&cp, fconfig, file);
  if (strcmp (arg, "stream") == 0)
    sep->se_socktype = SOCK_STREAM;
  else if (strcmp (arg, "dgram") == 0)
    sep->se_socktype = SOCK_DGRAM;
  else if (strcmp (arg, "rdm") == 0)
    sep->se_socktype = SOCK_RDM;
  else if (strcmp (arg, "seqpacket") == 0)
    sep->se_socktype = SOCK_SEQPACKET;
  else if (strcmp (arg, "raw") == 0)
    sep->se_socktype = SOCK_RAW;
  else
    sep->se_socktype = -1;
  sep->se_proto = newstr (sskip (&cp, fconfig, file));
  arg = sskip (&cp, fconfig, file);
  sep->se_wait = strcmp (arg, "wait") == 0;
  if (ISMUX (sep))
    {
      /*
       * Silently enforce "nowait" for TCPMUX services since
       * they don't have an assigned port to listen on.
       */
      sep->se_wait = 0;

      if (strcmp (sep->se_proto, "tcp"))
	{
	  syslog (LOG_ERR, "%s: bad protocol for tcpmux service %s",
		  file, sep->se_service);
	  goto more;
	}
      if (sep->se_socktype != SOCK_STREAM)
	{
	  syslog (LOG_ERR,
		  "%s: bad socket type for tcpmux service %s",
		  file, sep->se_service);
	  goto more;
	}
    }
  sep->se_user = newstr (sskip (&cp, fconfig, file));
  sep->se_server = newstr (sskip (&cp, fconfig, file));
  if (strcmp (sep->se_server, "internal") == 0)
    {
      struct biltin *bi;

      for (bi = biltins; bi->bi_service; bi++)
	if (bi->bi_socktype == sep->se_socktype
	    && strcmp (bi->bi_service, sep->se_service) == 0)
	  break;
      if (bi->bi_service == 0)
	{
	  syslog (LOG_ERR, "internal service %s unknown", sep->se_service);
	  goto more;
	}
      sep->se_bi = bi;
      sep->se_wait = bi->bi_wait;
    } else
      sep->se_bi = NULL;
  argc = 0;
  for (arg = skip (&cp, fconfig); cp; arg = skip (&cp, fconfig))
    if (argc < MAXARGV)
      sep->se_argv[argc++] = newstr (arg);
  while (argc <= MAXARGV)
    sep->se_argv[argc++] = NULL;
  return sep;
}

void
freeconfig (struct servtab *cp)
{
  int i;

  if (cp->se_service)
    free (cp->se_service);
  if (cp->se_proto)
    free (cp->se_proto);
  if (cp->se_user)
    free (cp->se_user);
  if (cp->se_server)
    free (cp->se_server);
  for (i = 0; i < MAXARGV; i++)
    if (cp->se_argv[i])
      free (cp->se_argv[i]);
}


/*
 * Safe skip - if skip returns null, log a syntax error in the
 * configuration file and exit.
 */
char *
sskip (char **cpp, FILE *fconfig, const char *file)
{
  char *cp;

  cp = skip (cpp, fconfig);
  if (cp == NULL)
    {
      syslog (LOG_ERR, "%s: syntax error", file);
      exit(-1);
    }
  return cp;
}

char *
skip (char **cpp, FILE *fconfig)
{
  char *cp = *cpp;
  char *start;

again:
  while (*cp == ' ' || *cp == '\t')
    cp++;
  if (*cp == '\0')
    {
      int c;

      c = getc (fconfig);
      (void) ungetc (c, fconfig);
      if (c == ' ' || c == '\t')
	if ((cp = nextline (fconfig)))
	  goto again;
      *cpp = (char *)0;
      return ((char *)0);
    }
  start = cp;
  while (*cp && *cp != ' ' && *cp != '\t')
    cp++;
  if (*cp != '\0')
    *cp++ = '\0';
  *cpp = cp;
  return start;
}

char *
nextline (FILE *fd)
{
  char *cp;

  if (fgets (line, sizeof line, fd) == NULL)
    return ((char *)0);
  cp = strchr (line, '\n');
  if (cp)
    *cp = '\0';
  return line;
}

char *
newstr (const char *cp)
{
  char *s;
  if ((s = strdup (cp ? cp : "")))
    return s;
  syslog (LOG_ERR, "strdup: %m");
  exit (-1);
}

void
set_proc_title (char *a, int s)
{
  int size;
  char *cp;
  struct sockaddr_in lsin;
  char buf[80];

  cp = Argv[0];
  size = sizeof lsin;
  if (getpeername (s, (struct sockaddr *)&lsin, &size) == 0)
    snprintf (buf, sizeof buf, "-%s [%s]", a, inet_ntoa (lsin.sin_addr));
  else
    snprintf (buf, sizeof buf, "-%s", a);
  strncpy (cp, buf, LastArg - cp);
  cp += strlen (cp);
  while (cp < LastArg)
    *cp++ = ' ';
}

/*
 * Internet services provided internally by inetd:
 */
#define	BUFSIZE	8192

/* ARGSUSED */
/* Echo service -- echo data back */
void
echo_stream (int s, struct servtab *sep)
{
  char buffer[BUFSIZE];
  int i;

  set_proc_title (sep->se_service, s);
  while ((i = read (s, buffer, sizeof buffer)) > 0
	 && write (s, buffer, i) > 0)
    ;
  exit (0);
}

/* ARGSUSED */
/* Echo service -- echo data back */
void
echo_dg (int s, struct servtab *sep)
{
  char buffer[BUFSIZE];
  int i, size;
  struct sockaddr sa;

  (void)sep;
  size = sizeof sa;
  if ((i = recvfrom (s, buffer, sizeof buffer, 0, &sa, &size)) < 0)
    return;
  (void) sendto (s, buffer, i, 0, &sa, sizeof sa);
}

/* ARGSUSED */
/* Discard service -- ignore data */
void
discard_stream (int s, struct servtab *sep)
{
  int ret;
  char buffer[BUFSIZE];

  set_proc_title (sep->se_service, s);
  while (1)
    {
      while ((ret = read (s, buffer, sizeof buffer)) > 0)
	;
      if (ret == 0 || errno != EINTR)
	break;
    }
  exit (0);
}

/* ARGSUSED */
void
/* Discard service -- ignore data */
discard_dg (int s, struct servtab *sep)
{
  char buffer[BUFSIZE];
  (void)sep; /* shutup gcc */
  (void) read (s, buffer, sizeof buffer);
}

#include <ctype.h>
#define LINESIZ 72
char ring[128];
char *endring;

void
initring (void)
{
  int i;

  endring = ring;

  for (i = 0; i <= 128; ++i)
    if (isprint (i))
      *endring++ = i;
}

/* ARGSUSED */
/* Character generator */
void
chargen_stream (int s, struct servtab *sep)
{
  int len;
  char *rs, text[LINESIZ+2];

  set_proc_title (sep->se_service, s);

  if (!endring)
    {
      initring ();
      rs = ring;
    }

  text[LINESIZ] = '\r';
  text[LINESIZ + 1] = '\n';
  for (rs = ring;;)
    {
      if ((len = endring - rs) >= LINESIZ)
	memmove (text, rs, LINESIZ);
      else
	{
	  memmove (text, rs, len);
	  memmove (text + len, ring, LINESIZ - len);
	}
      if (++rs == endring)
	rs = ring;
      if (write (s, text, sizeof text) != sizeof text)
	break;
	}
  exit (0);
}

/* ARGSUSED */
/* Character generator */
void
chargen_dg (int s, struct servtab *sep)
{
  struct sockaddr sa;
  static char *rs;
  int len, size;
  char text[LINESIZ+2];

  (void)sep; /* shutup gcc */
  if (endring == 0)
    {
      initring ();
      rs = ring;
    }

  size = sizeof sa;
  if (recvfrom (s, text, sizeof text, 0, &sa, &size) < 0)
    return;

  if ((len = endring - rs) >= LINESIZ)
    memmove (text, rs, LINESIZ);
  else
    {
      memmove (text, rs, len);
      memmove (text + len, ring, LINESIZ - len);
    }
  if (++rs == endring)
    rs = ring;
  text[LINESIZ] = '\r';
  text[LINESIZ + 1] = '\n';
  (void) sendto (s, text, sizeof text, 0, &sa, sizeof sa);
}

/*
 * Return a machine readable date and time, in the form of the
 * number of seconds since midnight, Jan 1, 1900.  Since gettimeofday
 * returns the number of seconds since midnight, Jan 1, 1970,
 * we must add 2208988800 seconds to this figure to make up for
 * some seventy years Bell Labs was asleep.
 */

long
machtime (void)
{
  struct timeval tv;

  if (gettimeofday (&tv, (struct timezone *)0) < 0)
    {
      if (debug)
	fprintf (stderr, "Unable to get time of day\n");
      return 0L;
    }
#define	OFFSET ((u_long)25567 * 24*60*60)
  return (htonl ((long)(tv.tv_sec + OFFSET)));
#undef OFFSET
}

/* ARGSUSED */
void
machtime_stream (int s, struct servtab *sep)
{
  long result;

  (void)sep; /* shutup gcc */
  result = machtime ();
  (void) write (s, (char *) &result, sizeof result);
}

/* ARGSUSED */
void
machtime_dg (int s, struct servtab *sep)
{
  long result;
  struct sockaddr sa;
  int size;

  (void)sep; /* shutup gcc */
  size = sizeof sa;
  if (recvfrom (s, (char *)&result, sizeof result, 0, &sa, &size) < 0)
    return;
  result = machtime ();
  (void) sendto (s, (char *) &result, sizeof result, 0, &sa, sizeof sa);
}

/* ARGSUSED */
void
/* Return human-readable time of day */
daytime_stream (int s, struct servtab *sep)
{
  char buffer[256];
  time_t lclock;

  (void)sep; /*shutup gcc*/
  lclock = time ((time_t *) 0);

  (void) sprintf (buffer, "%.24s\r\n", ctime(&lclock));
  (void) write (s, buffer, strlen(buffer));
}

/* ARGSUSED */
/* Return human-readable time of day */
void
daytime_dg(int s, struct servtab *sep)
{
  char buffer[256];
  time_t lclock;
  struct sockaddr sa;
  int size;

  (void)sep; /* shutup gcc */
  lclock = time ((time_t *) 0);

  size = sizeof sa;
  if (recvfrom (s, buffer, sizeof buffer, 0, &sa, &size) < 0)
    return;
  (void) sprintf (buffer, "%.24s\r\n", ctime (&lclock));
  (void) sendto (s, buffer, strlen(buffer), 0, &sa, sizeof sa);
}

/*
 * print_service:
 *	Dump relevant information to stderr
 */
void
print_service (const char *file, const char *action, struct servtab *sep)
{
  fprintf (stderr,
	   "%s:%s: %s proto=%s, wait=%d, user=%s builtin=%lx server=%s\n",
	   file, action, sep->se_service, sep->se_proto,
	   sep->se_wait, sep->se_user, (long)sep->se_bi, sep->se_server);
}

/*
 *  Based on TCPMUX.C by Mark K. Lottor November 1988
 *  sri-nic::ps:<mkl>tcpmux.c
 */


/* # of characters upto \r,\n or \0 */
static int
fd_getline (int fd, char *buf, int len)
{
  int count = 0, n;

  do {
    n = read (fd, buf, len-count);
    if (n == 0)
      return count;
    if (n < 0)
      return -1;
    while (--n >= 0)
      {
	if (*buf == '\r' || *buf == '\n' || *buf == '\0')
	  return count;
	count++;
	buf++;
      }
  } while (count < len);
  return count;
}

#define MAX_SERV_LEN	(256+2)		/* 2 bytes for \r\n */

#define strwrite(fd, buf)	(void) write(fd, buf, sizeof(buf)-1)

void
tcpmux(int s, struct servtab *sep)
{
  char service[MAX_SERV_LEN+1];
  int len;

  /* Get requested service name */
  if ((len = fd_getline (s, service, MAX_SERV_LEN)) < 0)
    {
      strwrite (s, "-Error reading service name\r\n");
      _exit (1);
    }
  service[len] = '\0';

  if (debug)
    fprintf (stderr, "tcpmux: someone wants %s\n", service);

  /*
   * Help is a required command, and lists available services,
   * one per line.
   */
  if (!strcasecmp (service, "help"))
    {
      for (sep = servtab; sep; sep = sep->se_next)
	{
	  if (!ISMUX (sep))
	    continue;
	  (void)write (s, sep->se_service, strlen (sep->se_service));
	  strwrite (s, "\r\n");
	}
      _exit (1);
    }

  /* Try matching a service in inetd.conf with the request */
  for (sep = servtab; sep; sep = sep->se_next)
    {
      if (!ISMUX (sep))
	continue;
      if (!strcasecmp (service, sep->se_service))
	{
	  if (ISMUXPLUS (sep))
	    {
	      strwrite (s, "+Go\r\n");
	    }
	  run_service (s, sep);
	  return;
	}
    }
  strwrite (s, "-Service not available\r\n");
  exit (1);
}