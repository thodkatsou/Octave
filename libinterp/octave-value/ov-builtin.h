/*

Copyright (C) 1996-2017 John W. Eaton

This file is part of Octave.

Octave is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Octave is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Octave; see the file COPYING.  If not, see
<http://www.gnu.org/licenses/>.

*/

#if ! defined (octave_ov_builtin_h)
#define octave_ov_builtin_h 1

#include "octave-config.h"

#include <list>
#include <set>
#include <string>

#include "ov-fcn.h"
#include "ov-typeinfo.h"

class octave_value;
class octave_value_list;
class jit_type;

namespace octave
{
  class interpreter;
}

// Builtin functions.

class
OCTINTERP_API
octave_builtin : public octave_function
{
public:

  octave_builtin (void) : octave_function (), f (0), file (), jtype (0) { }

  typedef octave_value_list (*meth) (octave::interpreter&,
                                     const octave_value_list&, int);

  typedef octave_value_list (*fcn) (const octave_value_list&, int);

  octave_builtin (fcn ff, const std::string& nm = "",
                  const std::string& ds = "")
    : octave_function (nm, ds), f (ff), m (0), file (), jtype (0) { }

  octave_builtin (meth mm, const std::string& nm = "",
                  const std::string& ds = "")
    : octave_function (nm, ds), f (0), m (mm), file (), jtype (0) { }

  octave_builtin (fcn ff, const std::string& nm, const std::string& fnm,
                  const std::string& ds)
    : octave_function (nm, ds), f (ff), m (0), file (fnm), jtype (0) { }

  octave_builtin (meth mm, const std::string& nm, const std::string& fnm,
                  const std::string& ds)
    : octave_function (nm, ds), f (0), m (mm), file (fnm), jtype (0) { }

  // No copying!

  octave_builtin (const octave_builtin& ob) = delete;

  octave_builtin& operator = (const octave_builtin& ob) = delete;

  ~octave_builtin (void) = default;

  std::string src_file_name (void) const { return file; }

  octave_function * function_value (bool = false) { return this; }

  bool is_builtin_function (void) const { return true; }

  octave_value_list call (int nargout, const octave_value_list& args);

  jit_type * to_jit (void) const;

  void stash_jit (jit_type& type);

  fcn function (void) const;

  meth method (void) const;

  void push_dispatch_class (const std::string& dispatch_type);

  bool handles_dispatch_class (const std::string& dispatch_type) const;

protected:

  // A pointer to the actual function.
  fcn f;
  meth m;

  // The name of the file where this function was defined.
  std::string file;

  // The types this function has been declared to handle (if any).
  std::set<std::string> dispatch_classes;

  // A pointer to the jit type that represents the function.
  jit_type *jtype;

private:

  DECLARE_OV_TYPEID_FUNCTIONS_AND_DATA
};

#endif
