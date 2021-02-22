/* CodeView structures and constants
 * Copyright (c) 2021 Mark Harmstone
 *
 * This file is part of GCC.
 *
 * GCC is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version.
 *
 * GCC is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GCC; see the file COPYING3.  If not see
 * <http://www.gnu.org/licenses/>.  */

#ifndef GCC_PDBOUT_H
#define GCC_PDBOUT_H 1

#define S_END				0x0006
#define S_LDATA32			0x110c
#define S_GDATA32			0x110d
#define S_LPROC32			0x110f
#define S_GPROC32			0x1110

/* Format version as of MSVC 7 */
#define CV_SIGNATURE_C13	4

#define DEBUG_S_SYMBOLS			0xf1

struct pdb_func
{
  struct pdb_func *next;
  char *name;
  int num;
  unsigned int public_flag;
  struct pdb_type *type;
};

struct pdb_global_var
{
  struct pdb_global_var *next;
  char *name;
  char *asm_name;
  unsigned int public_flag;
  struct pdb_type *type;
};

struct pdb_type
{
  struct pdb_type *next;
  struct pdb_type *next2;
  uint16_t id;
  tree_node *tree;
  uint16_t cv_type;
  uint8_t data[1];
};

#define CV_BUILTIN_TYPE_VOID			0x0003
#define CV_BUILTIN_TYPE_HRESULT			0x0008
#define CV_BUILTIN_TYPE_SIGNED_CHARACTER	0x0010
#define CV_BUILTIN_TYPE_INT16SHORT		0x0011
#define CV_BUILTIN_TYPE_INT32LONG		0x0012
#define CV_BUILTIN_TYPE_INT64QUAD		0x0013
#define CV_BUILTIN_TYPE_UINT64QUAD		0x0023
#define CV_BUILTIN_TYPE_UNSIGNED_CHARACTER	0x0020
#define CV_BUILTIN_TYPE_UINT16SHORT		0x0021
#define CV_BUILTIN_TYPE_UINT32LONG		0x0022
#define CV_BUILTIN_TYPE_BOOLEAN8		0x0030
#define CV_BUILTIN_TYPE_BOOLEAN16		0x0031
#define CV_BUILTIN_TYPE_BOOLEAN32		0x0032
#define CV_BUILTIN_TYPE_BOOLEAN64		0x0033
#define CV_BUILTIN_TYPE_BOOLEAN128		0x0034
#define CV_BUILTIN_TYPE_FLOAT16			0x0046
#define CV_BUILTIN_TYPE_FLOAT32			0x0040
#define CV_BUILTIN_TYPE_FLOAT48			0x0044
#define CV_BUILTIN_TYPE_FLOAT64			0x0041
#define CV_BUILTIN_TYPE_FLOAT80			0x0042
#define CV_BUILTIN_TYPE_FLOAT128		0x0043
#define CV_BUILTIN_TYPE_COMPLEX32		0x0050
#define CV_BUILTIN_TYPE_COMPLEX64		0x0051
#define CV_BUILTIN_TYPE_COMPLEX80		0x0052
#define CV_BUILTIN_TYPE_COMPLEX128		0x0053
#define CV_BUILTIN_TYPE_COMPLEX48		0x0054
#define CV_BUILTIN_TYPE_COMPLEX16		0x0056
#define CV_BUILTIN_TYPE_SBYTE			0x0068
#define CV_BUILTIN_TYPE_BYTE			0x0069
#define CV_BUILTIN_TYPE_NARROW_CHARACTER	0x0070
#define CV_BUILTIN_TYPE_WIDE_CHARACTER		0x0071
#define CV_BUILTIN_TYPE_INT16			0x0072
#define CV_BUILTIN_TYPE_UINT16			0x0073
#define CV_BUILTIN_TYPE_INT32			0x0074
#define CV_BUILTIN_TYPE_UINT32			0x0075
#define CV_BUILTIN_TYPE_INT64			0x0076
#define CV_BUILTIN_TYPE_UINT64			0x0077
#define CV_BUILTIN_TYPE_INT128			0x0078
#define CV_BUILTIN_TYPE_UINT128			0x0079
#define CV_BUILTIN_TYPE_CHARACTER16		0x007a
#define CV_BUILTIN_TYPE_CHARACTER32		0x007b

// from CV_prmode_e in cvdump
#define CV_TM_NPTR			1
#define CV_TM_NPTR32			4
#define CV_TM_NPTR64			6

struct pdb_type_tree_hasher : nofree_ptr_hash <struct pdb_type>
{
  typedef struct pdb_type *value_type;
  typedef tree compare_type;

  static inline hashval_t hash (compare_type);

  static inline hashval_t hash (const value_type t)
  {
    return hash (t->tree);
  }

  static inline bool equal (const value_type, compare_type);
};

#endif
