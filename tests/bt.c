/* libunwind - a platform-independent unwind library
   Copyright (C) 2001-2003 Hewlett-Packard Co
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

This file is part of libunwind.

Copyright (c) 2002 Hewlett-Packard Co.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#if HAVE_EXECINFO_H
# include <execinfo.h>
#else
  extern int backtrace (void **, int);
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libunwind.h>

#if UNW_TARGET_X86
# define STACK_SIZE	(128*1024)	/* On x86, SIGSTKSZ is too small */
#else
# define STACK_SIZE	SIGSTKSZ
#endif

#define panic(args...)				\
	{ fprintf (stderr, args); exit (-1); }

#ifndef HAVE_SIGHANDLER_T
typedef RETSIGTYPE (*sighandler_t) (int);
#endif

static void
do_backtrace (void)
{
  char buf[512], name[256];
  unw_word_t ip, sp, off;
  unw_cursor_t cursor;
  unw_proc_info_t pi;
  unw_context_t uc;
  int ret;

  unw_getcontext (&uc);
  if (unw_init_local (&cursor, &uc) < 0)
    panic ("unw_init_local failed!\n");

  do
    {
      unw_get_reg (&cursor, UNW_REG_IP, &ip);
      unw_get_reg (&cursor, UNW_REG_SP, &sp);
      buf[0] = '\0';
      if (unw_get_proc_name (&cursor, name, sizeof (name), &off) == 0)
	{
	  if (off)
	    snprintf (buf, sizeof (buf), "<%s+0x%lx>", name, (long) off);
	  else
	    snprintf (buf, sizeof (buf), "<%s>", name);
	}
      printf ("%016lx %-32s (sp=%016lx)\n", (long) ip, buf, (long) sp);

      unw_get_proc_info (&cursor, &pi);
      printf ("\tproc=%lx-%lx\n\thandler=%lx lsda=%lx gp=%lx",
	      (long) pi.start_ip, (long) pi.end_ip,
	      (long) pi.handler, (long) pi.lsda, (long) pi.gp);

#if UNW_TARGET_IA64
      {
	unw_word_t bsp;

	unw_get_reg (&cursor, UNW_IA64_BSP, &bsp);
	printf (" bsp=%lx", bsp);
      }
#endif
      printf ("\n");

      ret = unw_step (&cursor);
      if (ret < 0)
	{
	  unw_get_reg (&cursor, UNW_REG_IP, &ip);
	  printf ("FAILURE: unw_step() returned %d for ip=%lx\n",
		  ret, (long) ip);
	}
    }
  while (ret > 0);
}

static void
foo (void)
{
  void *buffer[20];
  int i, n;

  printf ("\texplicit backtrace:\n");
  do_backtrace ();

  printf ("\n\tvia backtrace():\n");
  n = backtrace (buffer, 20);
  for (i = 0; i < n; ++i)
    printf ("[%d] ip=%p\n", i, buffer[i]);
}

void
sighandler (int signal, void *siginfo, void *context)
{
  ucontext_t *uc = context;
  int sp;

  printf ("sighandler: got signal %d, sp=%p", signal, &sp);
#if UNW_TARGET_IA64
# if defined(__linux__)
  printf (" @ %lx", uc->uc_mcontext.sc_ip);
# else
  {
    uint16_t reason;
    uint64_t ip;

    __uc_get_reason (uc, &reason);
    __uc_get_ip (uc, &ip);
    printf (" @ %lx (reason=%d)", ip, reason);
  }
# endif
#elif UNW_TARGET_X86
  printf (" @ %lx", (unsigned long) uc->uc_mcontext.gregs[REG_EIP]);
#endif
  printf ("\n");

  do_backtrace();
}

int
main (int argc, char **argv)
{
  struct sigaction act;
  stack_t stk;

  printf ("Normal backtrace:\n");
  foo ();

  memset (&act, 0, sizeof (act));
  act.sa_handler = (void (*)(int)) sighandler;
  act.sa_flags = SA_SIGINFO;
  if (sigaction (SIGTERM, &act, NULL) < 0)
    panic ("sigaction: %s\n", strerror (errno));

  printf ("\nBacktrace across signal handler:\n");
  kill (getpid (), SIGTERM);

  printf ("\nBacktrace across signal handler on alternate stack:\n");
  stk.ss_sp = malloc (STACK_SIZE);
  if (!stk.ss_sp)
    panic ("failed to allocate SIGSTKSZ (%u) bytes\n", SIGSTKSZ);
  stk.ss_size = STACK_SIZE;
  stk.ss_flags = 0;
  if (sigaltstack (&stk, NULL) < 0)
    panic ("sigaltstack: %s\n", strerror (errno));

  memset (&act, 0, sizeof (act));
  act.sa_handler = (void (*)(int)) sighandler;
  act.sa_flags = SA_ONSTACK | SA_SIGINFO;
  if (sigaction (SIGTERM, &act, NULL) < 0)
    panic ("sigaction: %s\n", strerror (errno));
  kill (getpid (), SIGTERM);

  return 0;
}
