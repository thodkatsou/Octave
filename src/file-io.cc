// file-io.cc                                             -*- C++ -*-
/*

Copyright (C) 1993, 1994, 1995 John W. Eaton

This file is part of Octave.

Octave is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

Octave is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with Octave; see the file COPYING.  If not, write to the Free
Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

*/

// Written by John C. Campbell <jcc@che.utexas.edu>.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <DLList.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <strstream.h>
#include <ctype.h>

#include "dMatrix.h"

#include "statdefs.h"
#include "file-io.h"
#include "input.h"
#include "octave-hist.h"
#include "tree-const.h"
#include "error.h"
#include "help.h"
#include "utils.h"
#include "pager.h"
#include "defun.h"
#include "sysdep.h"
#include "mappers.h"
#include "variables.h"

// keeps a count of args sent to printf or scanf
static int fmt_arg_count = 0;

// double linked list containing relevant information about open files
static DLList <file_info> file_list;

file_info::file_info (void)
{
  file_number = -1;
  file_name = 0;
  file_fptr = 0;
  file_mode = 0;
}

file_info::file_info (int n, const char *nm, FILE *t, const char *md)
{
  file_number = n;
  file_name = strsave (nm);
  file_fptr = t;
  file_mode = strsave (md);
}

file_info::file_info (const file_info& f)
{
  file_number = f.file_number;
  file_name = strsave (f.file_name);
  file_fptr = f.file_fptr;
  file_mode = strsave (f.file_mode);
}

file_info&
file_info::operator = (const file_info& f)
{
  if (this != & f)
    {
      file_number = f.file_number;
      delete [] file_name;
      file_name = strsave (f.file_name);
      file_fptr = f.file_fptr;
      delete [] file_mode;
      file_mode = strsave (f.file_mode);
    }
  return *this;
}

file_info::~file_info (void)
{
  delete [] file_name;
  delete [] file_mode;
}

int
file_info::number (void) const
{
  return file_number;
}

const char *
file_info::name (void) const
{
  return file_name;
}

FILE *
file_info::fptr (void) const
{
  return file_fptr;
}

const char *
file_info::mode (void) const
{
  return file_mode;
}

void
initialize_file_io (void)
{
  file_info octave_stdin (0, "stdin", stdin, "r");
  file_info octave_stdout (1, "stdout", stdout, "w");
  file_info octave_stderr (2, "stderr", stderr, "w");

  file_list.append (octave_stdin);
  file_list.append (octave_stdout);
  file_list.append (octave_stderr);
}

// Given a file name or number, return a pointer to the corresponding
// open file.  If the file has not already been opened, return NULL.

Pix
return_valid_file (const tree_constant& arg)
{
  if (arg.is_string ())
    {
      Pix p = file_list.first ();
      file_info file;
      int file_count = file_list.length ();
      for (int i = 0; i < file_count; i++)
	{
	  char *file_name = arg.string_value ();
	  file = file_list (p);
	  if (strcmp (file.name (), file_name) == 0)
	    return p;
	  file_list.next (p);
	}
    }
  else
    {
      double file_num = arg.double_value ();

      if (! error_state)
	{
	  if (D_NINT (file_num) != file_num)
	    error ("file number not an integer value");
	  else
	    {
	      Pix p = file_list.first ();
	      file_info file;
	      int file_count = file_list.length ();
	      for (int i = 0; i < file_count; i++)
		{
		  file = file_list (p);
		  if (file.number () == file_num)
		    return p;
		  file_list.next (p);
		}
	      error ("no file with that number");
	    }
	}
      else
	error ("inapproriate file specifier");
    }

  return 0;
}

static Pix 
fopen_file_for_user (const char *name, const char *mode,
		     const char *warn_for)
{
  FILE *file_ptr = fopen (name, mode);
  if (file_ptr)
    {
      int file_number = file_list.length () + 1;

      file_info file (file_number, name, file_ptr, mode);
      file_list.append (file);
      
      Pix p = file_list.first ();
      file_info file_from_list;
      int file_count = file_list.length ();
      for (int i = 0; i < file_count; i++)
	{
	  file_from_list = file_list (p);
	  if (strcmp (file_from_list.name (), name) == 0)
	    return p;
	  file_list.next (p);
	}
    }

  error ("%s: unable to open file `%s'", warn_for, name);

  return 0;
}

static Pix
file_io_get_file (const tree_constant& arg, const char *mode,
		  const char *warn_for)
{
  Pix p = return_valid_file (arg);

  if (! p)
    {
      if (arg.is_string ())
	{
	  char *name = arg.string_value ();

	  struct stat buffer;
	  int status = stat (name, &buffer);

	  if (status == 0)
	    {
	      if ((buffer.st_mode & S_IFREG) == S_IFREG)
		p = fopen_file_for_user (name, mode, warn_for);
	      else
		error ("%s: invalid file type", warn_for);
	    }
	  else if (status < 0 && *mode != 'r')
	    p = fopen_file_for_user (name, mode, warn_for);
	  else
	    error ("%s: can't stat file `%s'", warn_for, name);
	}
      else
	error ("%s: invalid file specifier", warn_for);
    }

  return p;
}

static Octave_object
fclose_internal (const Octave_object& args)
{
  Octave_object retval;

  Pix p = return_valid_file (args(0));

  if (! p)
    return retval;

  file_info file = file_list (p);

  if (file.number () < 3)
    {
      warning ("fclose: can't close stdin, stdout, or stderr!");
      return retval;
    }

  int success = fclose (file.fptr ());
  file_list.del (p);

  if (success == 0)
    retval(0) = 1.0; // succeeded
  else
    {
      error ("fclose: error on closing file");
      retval(0) = 0.0; // failed
    }

  return retval;
}

DEFUN ("fclose", Ffclose, Sfclose, 1, 1,
  "fclose (FILENAME or FILENUM): close a file")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1)
    print_usage ("fclose");
  else
    retval = fclose_internal (args);

  return retval;
}

static Octave_object
fflush_internal (const Octave_object& args)
{
  Octave_object retval;

  Pix p = return_valid_file (args(0));

  if (! p)
    return retval;

  file_info file = file_list (p);

  if (strcmp (file.mode (), "r") == 0)
    {
      warning ("can't flush an input stream");
      return retval;
    }

  int success = 0;

  if (file.number () == 1)
    flush_output_to_pager ();
  else
    success = fflush (file.fptr ());

  if (success == 0)
    retval(0) = 1.0; // succeeded
  else
    {
      error ("fflush: write error");
      retval(0) = 0.0; // failed
    }

  return retval;
}

DEFUN ("fflush", Ffflush, Sfflush, 1, 1,
  "fflush (FILENAME or FILENUM): flush buffered data to output file")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1)
    print_usage ("fflush");
  else
    retval = fflush_internal (args);

  return retval;
}

static int
valid_mode (const char *mode)
{
  if (mode)
    {
      char m = mode[0];
      if (m == 'r' || m == 'w' || m == 'a')
	{
	  m = mode[1];
	  return (m == '\0' || (m == '+' && mode[2] == '\0'));
	}
    }
  return 0;
}

static Octave_object
fgets_internal (const Octave_object& args, int nargout)
{
  Octave_object retval;

  Pix p = file_io_get_file (args(0), "r", "fgets");
  
  if (! p)
    return retval;


  double dlen = args(1).double_value ();

  if (error_state)
    return retval;

  if (xisnan (dlen))
    {
      error ("fgets: NaN invalid as length");
      return retval;
    }

  int length = NINT (dlen);

  if ((double) length != dlen)
    {
      error ("fgets: length not an integer value");
      return retval;
    }

  file_info file = file_list (p);

  char string [length+1];
  char *success = fgets (string, length+1, file.fptr ());

  if (! success)
    {
      retval(0) = -1.0;
      return retval;
    }

  if (nargout == 2)
    retval(1) = (double) strlen (string);

  retval(0) = string;

  return retval;
}

DEFUN ("fgets", Ffgets, Sfgets, 2, 2,
  "[STRING, LENGTH] = fgets (FILENAME or FILENUM, LENGTH)\n\
\n\
read a string from a file")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 2)
    print_usage ("fgets");
  else
    retval = fgets_internal (args, nargout);

  return retval;
}

static Octave_object
fopen_internal (const Octave_object& args)
{
  Octave_object retval;
  Pix p;

  if (! args(0).is_string ())
    {
      error ("fopen: file name must be a string");
      return retval;
    }

  p = return_valid_file (args(0));

  if (p)
    {
      file_info file = file_list (p);

      retval(0) = (double) file.number ();

      return retval;
    }

  if (! args(1).is_string ())
    {
      error ("fopen: file mode must be a string");
      return retval;
    }

  char *name = args(0).string_value ();
  char *mode = args(1).string_value ();

  if (! valid_mode (mode))
    {
      error ("fopen: invalid mode");
      return retval;
    }

  struct stat buffer;
  if (stat (name, &buffer) == 0 && (buffer.st_mode & S_IFDIR) == S_IFDIR)
    {
      error ("fopen: can't open directory");
      return retval;
    }

  FILE *file_ptr = fopen (name, mode);

  if (! file_ptr)
    {
      error ("fopen: unable to open file `%s'", name);
      return retval;
    }

  int file_number =  file_list.length () + 1;

  file_info file (file_number, name, file_ptr, mode);
  file_list.append (file);

  retval(0) = (double) file_number;

  return retval;
}

DEFUN ("fopen", Ffopen, Sfopen, 2, 1,
  "FILENUM = fopen (FILENAME, MODE): open a file\n\
\n\
  Valid values for mode include:\n\
\n\
   r  : open text file for reading\n\
   w  : open text file for writing; discard previous contents if any\n\
   a  : append; open or create text file for writing at end of file\n\
   r+ : open text file for update (i.e., reading and writing)\n\
   w+ : create text file for update; discard previous contents if any\n\
   a+ : append; open or create text file for update, writing at end\n\n\
 Update mode permits reading from and writing to the same file.")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 2)
    print_usage ("fopen");
  else
    retval = fopen_internal (args);

  return retval;
}

static Octave_object
freport_internal (void)
{
  Octave_object retval;
  Pix p = file_list.first ();

  ostrstream output_buf;

  output_buf << "\n number  mode  name\n\n";

  int file_count = file_list.length ();
  for (int i = 0; i < file_count; i++)
    {
      file_info file = file_list (p);
      output_buf.form ("%7d%6s  %s\n", file.number (), file.mode (),
		       file.name ());
      file_list.next (p);
    }

  output_buf << "\n" << ends;
  maybe_page_output (output_buf);

  return retval;
}

DEFUN ("freport", Ffreport, Sfreport, 0, 1,
  "freport (): list open files and their status")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin > 0)
    warning ("freport: ignoring extra arguments");

  retval = freport_internal ();

  return retval;
}

static Octave_object
frewind_internal (const Octave_object& args)
{
  Octave_object retval;

  Pix p = file_io_get_file (args(0), "a+", "frewind");

  if (p)
    {
      file_info file = file_list (p);
      rewind (file.fptr ());
    }

  return retval;
}

DEFUN ("frewind", Ffrewind, Sfrewind, 1, 1,
  "frewind (FILENAME or FILENUM): set file position at beginning of file")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1)
    print_usage ("frewind");
  else
    retval = frewind_internal (args);

  return retval;
}

static Octave_object
fseek_internal (const Octave_object& args)
{
  Octave_object retval;

  int nargin = args.length ();

  Pix p = file_io_get_file (args(0), "a+", "fseek");

  if (! p)
    return retval;

  long origin = SEEK_SET;

  double doff = args(1).double_value ();

  if (error_state)
    return retval;

  if (xisnan (doff))
    {
      error ("fseek: NaN invalid as offset");
      return retval;
    }

  long offset = NINT (doff);

  if ((double) offset != doff)
    {
      error ("fseek: offset not an integer value");
      return retval;
    }

  if (nargin == 3)
    {
      double dorig = args(2).double_value ();

      if (error_state)
	return retval;

      if (xisnan (dorig))
	{
	  error ("fseek: NaN invalid as origin");
	  return retval;
	}

      origin = NINT (dorig);

      if ((double) dorig != origin)
	{
	  error ("fseek: origin not an integer value");
	  return retval;
	}

      if (origin == 0)
	origin = SEEK_SET;
      else if (origin == 1)
	origin = SEEK_CUR;
      else if (origin == 2)
	origin = SEEK_END;
      else
	{
	  error ("fseek: invalid value for origin");
	  return retval;
	}
    }

  file_info file = file_list (p);
  int success = fseek (file.fptr (), offset, origin);

  if (success == 0)
    retval(0) = 1.0; // succeeded
  else
    {
      error ("fseek: file error");
      retval(0) = 0.0; // failed
    }

  return retval;
}

DEFUN ("fseek", Ffseek, Sfseek, 3, 1,
  "fseek (FILENAME or FILENUM, OFFSET [, ORIGIN])\n\
\n\
set file position for reading or writing")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 2 && nargin != 3)
    print_usage ("fseek");
  else
    retval = fseek_internal (args);

  return retval;
}

// Tell current position of file.

static Octave_object
ftell_internal (const Octave_object& args)
{
  Octave_object retval;

  Pix p = file_io_get_file (args(0), "a+", "ftell");

  if (p)
    {
      file_info file = file_list (p);
      long offset = ftell (file.fptr ());

      retval(0) = (double) offset;

      if (offset == -1L)
	error ("ftell: write error");
    }

  return retval;
}

DEFUN ("ftell", Fftell, Sftell, 1, 1,
  "POSITION = ftell (FILENAME or FILENUM): returns the current file position")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1)
    print_usage ("ftell");
  else
    retval = ftell_internal (args);

  return retval;
}

void
close_files (void)
{
  Pix p = file_list.first ();

  int file_count = file_list.length ();
  for (int i = 0; i < file_count; i++)
    {
      if (p)
	{
	  file_info file = file_list (p);

	  if (i > 2)   // do not close stdin, stdout, stderr!
	    {
	      int success = fclose (file.fptr ());
	      if (success != 0)
		error ("closing %s", file.name ());
	    }

	  file_list.del (p);
	}
      else
	{
	  error ("inconsistent state for internal file list!");
	  break;
	}
    }
}

static int
process_printf_format (const char *s, const Octave_object& args,
		       ostrstream& sb, const char *type)
{
  ostrstream fmt;

  int nargin = args.length ();

  fmt << "%";  // do_printf() already blew past this one...

  int chars_from_fmt_str = 0;

 again:
  switch (*s)
    {
    case '+': case '-': case ' ': case '0': case '#':
      chars_from_fmt_str++;
      fmt << *s++;
      goto again;

    case '\0':
      goto invalid_format;

    default:
      break;
    }

  if (*s == '*')
    {
      if (fmt_arg_count > nargin)
	{
	  error ("%s: not enough arguments", type);
	  return -1;
	}

      double tmp_len = args(fmt_arg_count++).double_value ();

      if (error_state || xisnan (tmp_len))
	{
	  error ("%s: `*' must be replaced by an integer", type);
	  return -1;
	}

      fmt << NINT (tmp_len);
      s++;
      chars_from_fmt_str++;
    }
  else
    {
      while (*s != '\0' && isdigit (*s))
	{
	  chars_from_fmt_str++;
	  fmt << *s++;
	}
    }

  if (*s == '\0')
    goto invalid_format;

  if (*s == '.')
    {
      chars_from_fmt_str++;
      fmt << *s++;
    }

  if (*s == '*')
    {
      if (*(s-1) == '*')
	goto invalid_format;

      if (fmt_arg_count > nargin)
	{
	  error ("%s: not enough arguments", type);
	  return -1;
	}

      double tmp_len = args(fmt_arg_count++).double_value ();

      if (error_state || xisnan (tmp_len))
	{
	  error ("%s: `*' must be replaced by an integer", type);
	  return -1;
	}

      fmt << NINT (tmp_len);
      s++;
      chars_from_fmt_str++;
    }
  else
    {
      while (*s != '\0' && isdigit (*s))
	{
	  chars_from_fmt_str++;
	  fmt << *s++;
	}
    }

  if (*s == '\0')
    goto invalid_format;

  if (*s != '\0' && (*s == 'h' || *s == 'l' || *s == 'L'))
    {
      chars_from_fmt_str++;
      fmt << *s++;
    }

  if (*s == '\0')
    goto invalid_format;

  if (fmt_arg_count > nargin)
    {
      error ("%s: not enough arguments", type);
      return -1;
    }

  switch (*s)
    {
    case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
      {
	double d = args(fmt_arg_count++).double_value ();

	if (error_state || xisnan (d))
	  goto invalid_conversion;

	int val = NINT (d);

	if ((double) val != d)
	  goto invalid_conversion;
	else
	  {
	    chars_from_fmt_str++;
	    fmt << *s << ends;
	    char *tmp_fmt = fmt.str ();
	    sb.form (tmp_fmt, val);
	    delete [] tmp_fmt;
	    return chars_from_fmt_str;
	  }
      }

    case 'e': case 'E': case 'f': case 'g': case 'G':
      {
	double val = args(fmt_arg_count++).double_value ();

	if (error_state)
	  goto invalid_conversion;
	else
	  {
	    chars_from_fmt_str++;
	    fmt << *s << ends;
	    char *tmp_fmt = fmt.str ();
	    sb.form (tmp_fmt, val);
	    delete [] tmp_fmt;
	    return chars_from_fmt_str;
	  }
      }

    case 's':
      {
	char *val = args(fmt_arg_count++).string_value ();

	if (error_state)
	  goto invalid_conversion;
	else
	  {
	    chars_from_fmt_str++;
	    fmt << *s << ends;
	    char *tmp_fmt = fmt.str ();
	    sb.form (tmp_fmt, val);
	    delete [] tmp_fmt;
	    return chars_from_fmt_str;
	  }
      }

    case 'c':
      {
	char *val = args(fmt_arg_count++).string_value ();

	if (error_state || strlen (val) != 1)
	  goto invalid_conversion;
	else
	  {
	    chars_from_fmt_str++;
	    fmt << *s << ends;
	    char *tmp_fmt = fmt.str ();
	    sb.form (tmp_fmt, *val);
	    delete [] tmp_fmt;
	    return chars_from_fmt_str;
	  }
      }

    default:
      goto invalid_format;
   }

 invalid_conversion:
  error ("%s: invalid conversion", type);
  return -1;

 invalid_format:
  error ("%s: invalid format", type);
  return -1;
}

// Formatted printing to a file.

static Octave_object
do_printf (const char *type, const Octave_object& args, int nargout)
{
  Octave_object retval;
  fmt_arg_count = 0;
  char *fmt;
  file_info file;

  if (strcmp (type, "fprintf") == 0)
    {
      Pix p = file_io_get_file (args(0), "a+", type);

      if (! p)
	return retval;

      file = file_list (p);

      if (file.mode () == "r")
	{
	  error ("%s: file is read only", type);
	  return retval;
	}

      fmt = args(1).string_value ();

      if (error_state)
	{
	  error ("%s: format must be a string", type);
	  return retval;
	}

      fmt_arg_count += 2;
    }
  else
    {
      fmt = args(0).string_value ();

      if (error_state)
	{
	  error ("%s: invalid format string", type);
	  return retval;
	}

      fmt_arg_count++;
    }

// Scan fmt for % escapes and print out the arguments.

  ostrstream output_buf;

  char *ptr = fmt;

  for (;;)
    {
      char c;
      while ((c = *ptr++) != '\0' && c != '%')
	output_buf << c;

      if (c == '\0')
	break;

      if (*ptr == '%')
	{
	  ptr++;
	  output_buf << c;
	  continue;
	}

// We must be looking at a format specifier.  Extract it or fail.

      int status = process_printf_format (ptr, args, output_buf, type);

      if (status < 0)
	return retval;

      ptr += status;
    }

  output_buf << ends;
  if (strcmp (type, "printf") == 0
      || (strcmp (type, "fprintf") == 0 && file.number () == 1))
    {
      maybe_page_output (output_buf);
    }
  else if (strcmp (type, "fprintf") == 0)
    {
      char *msg = output_buf.str ();
      int success = fputs (msg, file.fptr ());
      if (success == EOF)
	warning ("%s: unknown failure writing to file", type);
      delete [] msg;
    }
  else if (strcmp (type, "sprintf") == 0)
    {
      char *msg = output_buf.str ();
      retval(0) = msg;
      delete [] msg;
    }

  return retval;
}

DEFUN ("fprintf", Ffprintf, Sfprintf, -1, 1,
  "fprintf (FILENAME or FILENUM, FORMAT, ...)")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin < 2)
    print_usage ("fprintf");
  else
    retval = do_printf ("fprintf", args, nargout);

  return retval;
}

// Formatted printing.

DEFUN ("printf", Fprintf, Sprintf, -1, 1,
  "printf (FORMAT, ...)")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin < 1)
    print_usage ("printf");
  else
    retval = do_printf ("printf", args, nargout);

  return retval;
}

// Formatted printing to a string.

DEFUN ("sprintf", Fsprintf, Ssprintf, -1, 1,
  "s = sprintf (FORMAT, ...)")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin < 1)
    print_usage ("sprintf");
  else
    retval = do_printf ("sprintf", args, nargout);

  return retval;
}

static int
process_scanf_format (const char *s, ostrstream& fmt,
		      const char *type, int nargout, FILE* fptr,
		      Octave_object& values)
{
  fmt << "%";

  int chars_from_fmt_str = 0;
  int store_value = 1;
  int string_width = 0;
  int success = 1;

  if (*s == '*')
    {
      store_value = 0;
      s++;
      chars_from_fmt_str++;
    }

  if (isdigit (*s))
    {
      ostrstream str_number;
      while (*s != '\0' && isdigit (*s))
	{
	  chars_from_fmt_str++;
	  str_number << *s;
	  fmt << *s++;
	}
      str_number << ends;
      char *number = str_number.str ();
      string_width = atoi (number);
      delete [] number;
    }

  if (*s == '\0')
    goto invalid_format;

  if (*s != '\0' && (*s == 'h' || *s == 'l' || *s == 'L'))
    {
      chars_from_fmt_str++;
      s++;
    }

  if (*s == '\0')
    goto invalid_format;

// Even if we don't have a place to store them, attempt to convert
// everything specified by the format string.

  if (fmt_arg_count > (nargout ? nargout : 1))
    store_value = 0;

  switch (*s)
    {
    case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
      {
	chars_from_fmt_str++;
	fmt << *s << ends;
	int temp;
	char *str = fmt.str ();
	success = fscanf (fptr, str, &temp);
	delete [] str;
	if (success > 0 && store_value)
	  values(fmt_arg_count++) = (double) temp;
      }
      break;

    case 'e': case 'E': case 'f': case 'g': case 'G':
      {
	chars_from_fmt_str++;
	fmt << 'l' << *s << ends;
	double temp;
	char *str = fmt.str ();
	success = fscanf (fptr, str, &temp);
	delete [] str;
	if (success > 0 && store_value)
	  values(fmt_arg_count++) = temp;
      }
      break;

    case 's':
      {
	if (string_width < 1)
	  {
// XXX FIXME XXX -- The code below is miscompiled on the Alpha with
// gcc 2.6.0, so that string_width is never incremented, even though
// reading the data works correctly.  One fix is to use a fixed-size
// buffer...
//	    string_width = 8192;

	    string_width = 0;
	    long original_position = ftell (fptr);

	    int c;

	    while ((c = getc (fptr)) != EOF && isspace (c))
	      ; // Don't count leading whitespace.

	    if (c != EOF)
	      string_width++;

	    for (;;)
	      {
		c = getc (fptr);
		if (c != EOF && ! isspace (c))
		  string_width++;
		else
		  break;
	      }

	    fseek (fptr, original_position, SEEK_SET);
	  }
	chars_from_fmt_str++;
	char temp [string_width+1];
	fmt << *s << ends;
	char *str = fmt.str ();
	success = fscanf (fptr, str, temp);
	delete [] str;
	temp[string_width] = '\0';
	if (success > 0 && store_value)
	  values(fmt_arg_count++) = temp;
      }
      break;

    case 'c':
      {
	if (string_width < 1)
	  string_width = 1;
	chars_from_fmt_str++;
	char temp [string_width+1];
	memset (temp, '\0', string_width+1);
	fmt << *s << ends;
	char *str = fmt.str ();
	success = fscanf (fptr, str, temp);
	delete [] str;
	temp[string_width] = '\0';
	if (success > 0 && store_value)
	  values(fmt_arg_count++) = temp;
      }
      break;

    default:
      goto invalid_format;
    }

  if (success > 0)
    return chars_from_fmt_str;
  else if (success == 0)
    warning ("%s: invalid conversion", type);
  else if (success == EOF)
    {
      if (strcmp (type, "fscanf") == 0)
	warning ("%s: end of file reached before final conversion", type);
      else if (strcmp (type, "sscanf") == 0)
	warning ("%s: end of string reached before final conversion", type);
      else if (strcmp (type, "scanf") == 0)
	warning ("%s: end of input reached before final conversion", type);
    }
  else
    {
    invalid_format:
      warning ("%s: invalid format", type);
    }

  return -1;
}

// Formatted reading from a file.

static Octave_object
do_scanf (const char *type, const Octave_object& args, int nargout)
{
  Octave_object retval;
  char *scanf_fmt = 0;
  char *tmp_file = 0;
  int tmp_file_open = 0;
  FILE *fptr = 0;
  file_info file;

  fmt_arg_count = 0;

  if (strcmp (type, "scanf") != 0)
    {
      scanf_fmt = args(1).string_value ();

      if (error_state)
	{
	  error ("%s: format must be a string", type);
	  return retval;
	}
    }

  int doing_fscanf = (strcmp (type, "fscanf") == 0);

  if (doing_fscanf)
    {
      Pix p = file_io_get_file (args(0), "r", type);

      if (! p)
	return retval;

      file = file_list (p);

      if (strcmp (file.mode (), "w") == 0 || strcmp (file.mode (), "a") == 0)
	{
	  error ("%s: this file is opened for writing only", type);
	  return retval;
	}

      fptr = file.fptr ();
    }

  if ((! fptr && args(0).is_string ())
      || (doing_fscanf && file.number () == 0))
    {
      char *string;

      if (strcmp (type, "scanf") == 0)
	scanf_fmt = args(0).string_value ();

      if (strcmp (type, "scanf") == 0
	  || (doing_fscanf && file.number () == 0))
	{
// XXX FIXME XXX -- this should probably be possible for more than
// just stdin/stdout pairs, using a list of output streams to flush.
// The list could be created with a function like iostream's tie().

	  flush_output_to_pager ();

	  string = gnu_readline ("");

	  if (string && *string)
	    maybe_save_history (string);
	}
      else
	string = args(0).string_value ();

      tmp_file = octave_tmp_file_name ();

      fptr = fopen (tmp_file, "w+");
      if (! fptr)
	{
	  error ("%s: error opening temporary file", type);
	  return retval;
	}
      tmp_file_open = 1;
      unlink (tmp_file);

      if (! string)
	{
	  error ("%s: no string to scan", type); 
	  return retval;
	}

      int success = fputs (string, fptr);
      fflush (fptr);
      rewind (fptr);

      if (success < 0)
	{
	  error ("%s: trouble writing temporary file", type);
	  fclose (fptr);
	  return retval;
	}
    }
  else if (! doing_fscanf)
    {
      error ("%s: first argument must be a string", type);
      return retval;
    }

// Scan scanf_fmt for % escapes and assign the arguments.

  retval.resize (nargout);

  char *ptr = scanf_fmt;

  for (;;)
    {
      ostrstream fmt;
      char c;
      while ((c = *ptr++) != '\0' && c != '%')
	fmt << c;

      if (c == '\0')
	break;

      if (*ptr == '%')
	{
	  ptr++;
	  fmt << c;
	  continue;
	}

// We must be looking at a format specifier.  Extract it or fail.

      int status = process_scanf_format (ptr, fmt, type, nargout,
					 fptr, retval);

      if (status < 0)
	{
	  if (fmt_arg_count == 0)
	    retval.resize (0);
	  break;
	}

      ptr += status;
    }

  if (tmp_file_open)
    fclose (fptr);

  return retval;
}

DEFUN ("fscanf", Ffscanf, Sfscanf, 2, -1,
  "[A, B, C, ...] = fscanf (FILENAME or FILENUM, FORMAT)")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1 && nargin != 2)
    print_usage ("fscanf");
  else
    retval = do_scanf ("fscanf", args, nargout);

  return retval;
}

// Formatted reading.

DEFUN ("scanf", Fscanf, Sscanf, 1, -1,
  "[A, B, C, ...] = scanf (FORMAT)")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1)
    print_usage ("scanf");
  else
    retval = do_scanf ("scanf", args, nargout);

  return retval;
}

// Formatted reading from a string.

DEFUN ("sscanf", Fsscanf, Ssscanf, 2, -1,
  "[A, B, C, ...] = sscanf (STRING, FORMAT)")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 2)
    print_usage ("sscanf");
  else
    retval = do_scanf ("sscanf", args, nargout);

  return retval;
}

// Find out how many elements are left to read.

static long
num_items_remaining (FILE *fptr, char *type)
{
  size_t size;

  if (strcasecmp (type, "uchar") == 0)
    size = sizeof (u_char);
  else if (strcasecmp (type, "char") == 0)
    size = sizeof (char);
  else if (strcasecmp (type, "short") == 0)
    size = sizeof (short);
  else if (strcasecmp (type, "ushort") == 0)
    size = sizeof (u_short);
  else if (strcasecmp (type, "int") == 0)
    size = sizeof (int);
  else if (strcasecmp (type, "uint") == 0)
    size = sizeof (u_int);
  else if (strcasecmp (type, "long") == 0)
    size = sizeof (long);
  else if (strcasecmp (type, "ulong") == 0)
    size = sizeof (u_long);
  else if (strcasecmp (type, "float") == 0)
    size = sizeof (float);
  else if (strcasecmp (type, "double") == 0)
    size = sizeof (double);
  else
    return 0;

  long curr_pos = ftell (fptr);

  fseek (fptr, 0, SEEK_END);
  long end_of_file = ftell (fptr);

  fseek (fptr, curr_pos, SEEK_SET);

  long len = end_of_file - curr_pos;

  return len / size;
}

// Read binary data from a file.
//
//   [data, count] = fread (fid, size, 'precision')
//
//     fid       : the file id from fopen
//     size      : the size of the matrix or vector or scaler to read
//
//                 n	  : reads n elements of a column vector
//                 inf	  : reads to the end of file (default)
//                 [m, n] : reads enough elements to fill the matrix
//                          the number of columns can be inf
//
//     precision : type of the element.  Can be:
//
//                 char, uchar, schar, short, ushort, int, uint,
//                 long, ulong, float, double
//
//                 Default  is uchar.
//
//     data	 : output data
//     count	 : number of elements read

static Octave_object
fread_internal (const Octave_object& args, int nargout)
{
  Octave_object retval;

  int nargin = args.length ();

  Pix p = file_io_get_file (args(0), "r", "fread");

  if (! p)
    return retval;

// Get type and number of bytes per element to read.
  char *prec = "uchar";
  if (nargin > 2)
    {
      prec = args(2).string_value ();

      if (error_state)
	{
	  error ("fread: precision must be a specified as a string");
	  return retval;
	}
    }

// Get file info.

  file_info file = file_list (p);

  FILE *fptr = file.fptr ();

// Set up matrix to read into.  If specified in arguments use that
// number, otherwise read everyting left in file.

  double dnr = 0.0;
  double dnc = 0.0;
  int nr;
  int nc;

  if (nargin > 1)
    {
      if (args(1).is_scalar_type ())
	{
	  dnr = args(1).double_value ();

	  if (error_state)
	    return retval;

	  dnc = 1.0;
	}
      else
	{
	  ColumnVector tmp = args(1).vector_value ();

	  if (error_state || tmp.length () != 2)
	    {
	      error ("fread: invalid size specification\n");
	      return retval;
	    }

	  dnr = tmp.elem (0);
	  dnc = tmp.elem (1);
	}

      if ((xisinf (dnr)) && (xisinf (dnc)))
	{
	  error ("fread: number of rows and columns cannot both be infinite");
	  return retval;
	}

      if (xisinf (dnr))
	{
	  if (xisnan (dnc))
	    {
	      error ("fread: NaN invalid as the number of columns");
	      return retval;
	    }
	  else
	    {
	      nc = NINT (dnc);
	      int n = num_items_remaining (fptr, prec);
	      nr = n / nc;
	      if (n > nr * nc)
		nr++;
	    }
	}
      else if (xisinf (dnc))
	{
	  if (xisnan (dnr))
	    {
	      error ("fread: NaN invalid as the number of rows");
	      return retval;
	    }
	  else
	    {
	      nr = NINT (dnr);
	      int n = num_items_remaining (fptr, prec);
	      nc = n / nr;
	      if (n > nc * nr)
		nc++;
	    }
	}
      else
	{
	  if (xisnan (dnr))
	    {
	      error ("fread: NaN invalid as the number of rows");
	      return retval;
	    }
	  else
	    nr = NINT (dnr);

	  if (xisnan (dnc))
	    {
	      error ("fread: NaN invalid as the number of columns");
	      return retval;
	    }
	  else
	    nc = NINT (dnc);
	}
    }
  else
    {
// No size parameter, read what's left of the file.
      nc = 1;
      int n = num_items_remaining (fptr, prec);
      nr = n / nc;
      if (n > nr * nc)
	nr++;
    }

  Matrix m (nr, nc, octave_NaN);

// Read data.

  int count = m.read (fptr, prec);

  if (nargout > 1)
    retval(1) = (double) count;

  retval(0) = m;

  return retval;
}

DEFUN ("fread", Ffread, Sfread, 3, 2,
  "[DATA, COUNT] = fread (FILENUM, SIZE, PRECISION)\n\
\n\
 Reads data in binary form of type PRECISION from a file.\n\
\n\
 FILENUM   : file number from fopen\n\
 SIZE      : size specification for the Data matrix\n\
 PRECISION : type of data to read, valid types are\n\
\n\
               'char',   'schar', 'short',  'int',  'long', 'float'\n\
               'double', 'uchar', 'ushort', 'uint', 'ulong'\n\
\n\
 DATA      : matrix in which the data is stored\n\
 COUNT     : number of elements read")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin < 1 || nargin > 3)
    print_usage ("fread");
  else
    retval = fread_internal (args, nargout);

  return retval;
}

// Write binary data to a file.
//
//   count = fwrite (fid, data, 'precision')
//
//    fid	: file id from fopen
//    Data	: data to be written
//    precision	: type of output element.  Can be:
//
//                char, uchar, schar, short, ushort, int, uint,
//                long, float, double
//
//                 Default is uchar.
//
//    count     : the number of elements written

static Octave_object
fwrite_internal (const Octave_object& args, int nargout)
{
  Octave_object retval;

  int nargin = args.length ();

  Pix p = file_io_get_file (args(0), "a+", "fwrite");

  if (! p)
    return retval;

// Get type and number of bytes per element to read.
  char *prec = "uchar";
  if (nargin > 2)
    {
      prec = args(2).string_value ();

      if (error_state)
	{
	  error ("fwrite: precision must be a specified as a string");
	  return retval;
	}
    }

  file_info file = file_list (p);

  Matrix m = args(1).matrix_value ();

  if (! error_state)
    {
      int count = m.write (file.fptr (), prec);

      retval(0) = (double) count;
    }

  return retval;
}

DEFUN ("fwrite", Ffwrite, Sfwrite, 3, 1,
  "COUNT = fwrite (FILENUM, DATA, PRECISION)\n\
\n\
 Writes data to a file in binary form of size PRECISION\n\
\n\
 FILENUM   : file number from fopen\n\
 DATA      : matrix of elements to be written\n\
 PRECISION : type of data to read, valid types are\n\
\n\
               'char',   'schar', 'short',  'int',  'long', 'float'\n\
               'double', 'uchar', 'ushort', 'uint', 'ulong'\n\
\n\
 COUNT     : number of elements written")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin < 2 || nargin > 3)
    print_usage ("fwrite");
  else
    retval = fwrite_internal (args, nargout);

  return retval;
}

// Check for an EOF condition on a file opened by fopen.
//
//   eof = feof (fid)
//
//     fid : file id from fopen
//     eof : non zero for an end of file condition

static Octave_object
feof_internal (const Octave_object& args, int nargout)
{
  Octave_object retval;

// Get file info.
  Pix p = return_valid_file (args(0));

  if (! p)
    return retval;

  file_info file = file_list (p);

  retval(0) = (double) feof (file.fptr ());

  return retval;
}

DEFUN ("feof", Ffeof, Sfeof, 1, 1,
  "ERROR = feof (FILENAME or FILENUM)\n\
\n\
 Returns a non zero value for an end of file condition for the\n\
 file specified by FILENAME or FILENUM from fopen")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1)
    print_usage ("feof");
  else
    retval = feof_internal (args, nargout);

  return retval;
}

// Check for an error condition on a file opened by fopen.
//
//   [message, errnum] = ferror (fid)
//
//     fid     : file id from fopen
//     message : system error message
//     errnum  : error number

static Octave_object
ferror_internal (const Octave_object& args, int nargout)
{
  Octave_object retval;

// Get file info.
  Pix p = return_valid_file (args(0));

  if (! p)
    return retval;

  file_info file = file_list (p);

  int ierr = ferror (file.fptr ());

  if (nargout > 1)
    retval(1) = (double) ierr;

  retval(0) = strsave (strerror (ierr));

  return retval;
}

DEFUN ("ferror", Fferror, Sferror, 1, 1,
  "ERROR = ferror (FILENAME or FILENUM)\n\
\n\
 Returns a non zero value for an error condition on the\n\
 file specified by FILENAME or FILENUM from fopen")
{
  Octave_object retval;

  int nargin = args.length ();

  if (nargin != 1)
    print_usage ("ferror");
  else
    retval = ferror_internal (args, nargout);

  return retval;
}

/*
;;; Local Variables: ***
;;; mode: C++ ***
;;; page-delimiter: "^/\\*" ***
;;; End: ***
*/
