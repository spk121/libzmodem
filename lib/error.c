/* error.c -- error handler for noninteractive utilities
   Copyright (C) 1990, 91, 92, 93, 94, 95, 96 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by David MacKenzie <djm@gnu.ai.mit.edu>.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>

#include <stdarg.h>
#define VA_START(args, lastarg) va_start(args, lastarg)

#include <stdlib.h>
#include <string.h>

#ifndef _
# define _(String) String
#endif

/* If NULL, error will flush stdout, then print on stderr the program
   name, a colon and a space.  Otherwise, error will call this
   function without parameters instead.  */
void (*error_print_progname) (
#if __STDC__ - 0
			      void
#endif
			      );

/* This variable is incremented each time `error' is called.  */
unsigned int error_message_count;

#define program_name program_invocation_name
#include <errno.h>

/* Print the program name and error message MESSAGE, which is a printf-style
   format string with optional args.
   If ERRNUM is nonzero, print its corresponding system error message.
   Exit with status STATUS if it is nonzero.  */
/* VARARGS */

void
error (int status, int errnum, const char *message, ...)
{
  va_list args;

  if (error_print_progname)
    (*error_print_progname) ();
  else
    {
      fflush (stdout);
      fprintf (stderr, "%s: ", program_name);
    }

  VA_START (args, message);
  vfprintf (stderr, message, args);
  va_end (args);

  ++error_message_count;
  if (errnum)
    fprintf (stderr, ": %s", strerror (errnum));
  putc ('\n', stderr);
  fflush (stderr);
  if (status)
    exit (status);
}

/* Sometimes we want to have at most one error per line.  This
   variable controls whether this mode is selected or not.  */
int error_one_per_line;

void
error_at_line (int status, int errnum, const char *file_name,
	       unsigned int line_number, const char *message, ...)
{
  va_list args;

  if (error_one_per_line)
    {
      static const char *old_file_name;
      static unsigned int old_line_number;

      if (old_line_number == line_number &&
	  (file_name == old_file_name || !strcmp (old_file_name, file_name)))
	/* Simply return and print nothing.  */
	return;

      old_file_name = file_name;
      old_line_number = line_number;
    }

  if (error_print_progname)
    (*error_print_progname) ();
  else
    {
      fflush (stdout);
      fprintf (stderr, "%s:", program_name);
    }

  if (file_name != NULL)
    fprintf (stderr, "%s:%d: ", file_name, line_number);

  VA_START (args, message);
  vfprintf (stderr, message, args);
  va_end (args);

  ++error_message_count;
  if (errnum)
    fprintf (stderr, ": %s", strerror (errnum));
  putc ('\n', stderr);
  fflush (stderr);
  if (status)
    exit (status);
}
