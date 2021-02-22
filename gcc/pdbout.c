/* Output CodeView debugging information from GNU compiler.
 * Copyright (C) 2021 Mark Harmstone
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

/* The CodeView structure is partially documented - definitions of structures
 * output below can be found at:
 * https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h
 */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "varasm.h"
#include "tree.h"
#include "debug.h"
#include "pdbout.h"
#include "function.h"
#include "output.h"
#include "target.h"

#define FUNC_BEGIN_LABEL	".Lstartfunc"
#define FUNC_END_LABEL		".Lendfunc"

static void pdbout_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
				   unsigned int column ATTRIBUTE_UNUSED,
				   const char *file ATTRIBUTE_UNUSED);
static void pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
				 const char *file ATTRIBUTE_UNUSED);
static void pdbout_finish (const char *filename);
static void pdbout_begin_function (tree func);
static void pdbout_late_global_decl (tree var);

static struct pdb_type *find_type (tree t);

static struct pdb_func *funcs = NULL, *cur_func = NULL;
static struct pdb_global_var *global_vars = NULL;
static struct pdb_type *types = NULL, *last_type = NULL;
static hash_table <pdb_type_tree_hasher> tree_hash_table (31);
static struct pdb_type *byte_type, *signed_byte_type, *wchar_type,
  *char16_type, *uint16_type, *int16_type, *char32_type, *uint32_type,
  *int32_type, *uint64_type, *int64_type, *uint128_type, *int128_type,
  *long_type, *ulong_type, *hresult_type;
static struct pdb_type *float16_type, *float32_type, *float48_type,
  *float64_type, *float80_type, *float128_type;
static struct pdb_type *bool8_type, *bool16_type, *bool32_type, *bool64_type,
  *bool128_type;
static struct pdb_type *complex16_type, *complex32_type, *complex48_type,
  *complex64_type, *complex80_type, *complex128_type;
static struct pdb_type *void_type, *nullptr_type;
static bool builtins_initialized = false;

const struct gcc_debug_hooks pdb_debug_hooks = {
  debug_nothing_charstar,	/* init */
  pdbout_finish,
  debug_nothing_charstar,	/* early_finish */
  debug_nothing_void,		/* assembly_start */
  debug_nothing_int_charstar,	/* define */
  debug_nothing_int_charstar,	/* undef */
  debug_nothing_int_charstar,	/* start_source_file */
  debug_nothing_int,		/* end_source_file */
  debug_nothing_int_int,	/* begin_block */
  debug_nothing_int_int,	/* end_block */
  debug_true_const_tree,	/* ignore_block */
  debug_nothing_int_int_charstar_int_bool,	/* source_line */
  pdbout_begin_prologue,
  debug_nothing_int_charstar,	/* end_prologue */
  debug_nothing_int_charstar,	/* begin_epilogue */
  pdbout_end_epilogue,
  pdbout_begin_function,
  debug_nothing_int,		/* end_function */
  debug_nothing_tree,		/* register_main_translation_unit */
  debug_nothing_tree,		/* function_decl */
  debug_nothing_tree,		/* early_global_decl */
  pdbout_late_global_decl,
  debug_nothing_tree_int,	/* type_decl */
  debug_nothing_tree_tree_tree_bool_bool,	/* imported_module_or_decl */
  debug_false_tree_charstarstar_uhwistar,	/* die_ref_for_decl */
  debug_nothing_tree_charstar_uhwi,	/* register_external_die */
  debug_nothing_tree,		/* deferred_inline_function */
  debug_nothing_tree,		/* outlining_inline_function */
  debug_nothing_rtx_code_label,	/* label */
  debug_nothing_int,		/* handle_pch */
  debug_nothing_rtx_insn,	/* var_location */
  debug_nothing_tree,		/* inline_entry */
  debug_nothing_tree,		/* size_function */
  debug_nothing_void,		/* switch_text_section */
  debug_nothing_tree_tree,	/* set_name */
  0,				/* start_end_main_source_file */
  TYPE_SYMTAB_IS_ADDRESS	/* tree_type_symtab_field */
};

/* Add label before function start */
static void
pdbout_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
		       unsigned int column ATTRIBUTE_UNUSED,
		       const char *file ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, FUNC_BEGIN_LABEL "%u:\n",
	   current_function_funcdef_no);
}

/* Add label after function end */
static void
pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
		     const char *file ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, FUNC_END_LABEL "%u:\n", current_function_funcdef_no);
}

/* Output PROCSYM32 structure, which describes a global function (S_GPROC32)
 * or a local (i.e. static) one (S_LPROC32). */
static void
pdbout_proc32 (struct pdb_func *func)
{
  size_t name_len = func->name ? strlen (func->name) : 0;
  uint16_t len = 40 + name_len, align;

  // start procedure

  if (len % 4 != 0)
    {
      align = 4 - (len % 4);
      len += 4 - (len % 4);
    }
  else
    align = 0;

  fprintf (asm_out_file, ".Lcvprocstart%u:\n", func->num);
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   (uint16_t) (len - sizeof (uint16_t)));	// reclen
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   func->public_flag ? S_GPROC32 : S_LPROC32);
  fprintf (asm_out_file, "\t.long\t0\n");	// pParent
  fprintf (asm_out_file, "\t.long\t[.Lcvprocend%u]-[.debug$S]\n",
	   func->num);	// pEnd
  fprintf (asm_out_file, "\t.long\t0\n");	// pNext
  fprintf (asm_out_file,
	   "\t.long\t[" FUNC_END_LABEL "%u]-[" FUNC_BEGIN_LABEL "%u]\n",
	   func->num, func->num);	// len
  fprintf (asm_out_file, "\t.long\t0\n");	// DbgStart
  fprintf (asm_out_file, "\t.long\t0\n");	// DbgEnd
  fprintf (asm_out_file, "\t.short\t0x%x\n", func->type ? func->type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding

  fprintf (asm_out_file, "\t.secrel32\t" FUNC_BEGIN_LABEL "%u\n",
	   func->num);	// offset
  fprintf (asm_out_file, "\t.secidx\t" FUNC_BEGIN_LABEL "%u\n",
	   func->num);	// section

  fprintf (asm_out_file, "\t.byte\t0\n");	// flags

  if (func->name)
    ASM_OUTPUT_ASCII (asm_out_file, func->name, name_len + 1);
  else
    fprintf (asm_out_file, "\t.byte\t0\n");

  for (unsigned int i = 0; i < align; i++)
    {
      fprintf (asm_out_file, "\t.byte\t0\n");
    }

  // end procedure

  fprintf (asm_out_file, ".Lcvprocend%u:\n", func->num);

  fprintf (asm_out_file, "\t.short\t0x2\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", S_END);
}

/* Output DATASYM32 structure, describing a global variable: either
 * one with file-level scope (S_LDATA32) or global scope (S_GDATA32). */
static void
pdbout_data32 (struct pdb_global_var *v)
{
  size_t name_len = strlen (v->name);
  uint16_t len;

  // Outputs DATASYM32 struct

  len = 15 + name_len;

  if (len % 4 != 0)
    len += 4 - (len % 4);

  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   (uint16_t) (len - sizeof (uint16_t)));	// reclen
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   v->public_flag ? S_GDATA32 : S_LDATA32);
  fprintf (asm_out_file, "\t.short\t0x%x\n", v->type ? v->type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");

  fprintf (asm_out_file, "\t.secrel32\t");	// off
  ASM_OUTPUT_LABELREF (asm_out_file, v->asm_name);
  fprintf (asm_out_file, "\n");
  fprintf (asm_out_file, "\t.secidx\t");	// section
  ASM_OUTPUT_LABELREF (asm_out_file, v->asm_name);
  fprintf (asm_out_file, "\n");

  ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);

  fprintf (asm_out_file, "\t.balign\t4\n");
}

/* Output the .debug$S section, which has everything except the
 * type definitions (global variables, functions, string table,
 * file checksums, line numbers).
 * The linker will extract this section from all the object
 * files, remove any duplicate data, resolve all addresses,
 * and output the resulting data into a PDB file. The section's
 * marked as "ndr", so even if the linker doesn't understand it,
 * the section won't make its way into final binary. */
static void
write_pdb_section (void)
{
  struct pdb_func *func;

  fprintf (asm_out_file, "\t.section\t.debug$S, \"ndr\"\n");
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_SIGNATURE_C13);
  fprintf (asm_out_file, "\t.long\t0x%x\n", DEBUG_S_SYMBOLS);
  fprintf (asm_out_file, "\t.long\t[.Lsymend]-[.Lsymstart]\n");

  fprintf (asm_out_file, ".Lsymstart:\n");

  while (global_vars)
    {
      struct pdb_global_var *n;

      pdbout_data32 (global_vars);

      n = global_vars->next;

      if (global_vars->name)
	free (global_vars->name);

      if (global_vars->asm_name)
	free (global_vars->asm_name);

      free (global_vars);

      global_vars = n;
    }

  func = funcs;
  while (func)
    {
      pdbout_proc32 (func);

      func = func->next;
    }

  fprintf (asm_out_file, ".Lsymend:\n");

  while (funcs)
    {
      struct pdb_func *n = funcs->next;

      if (funcs->name)
	free (funcs->name);

      free (funcs);

      funcs = n;
    }
}

/* We've finished compilation - output the .debug$S section
 * to the asm file. */
static void
pdbout_finish (const char *filename ATTRIBUTE_UNUSED)
{
  write_pdb_section ();
}

/* For a tree t, construct the name. */
static char *
get_tree_name (tree t)
{
  char *name;

  if (TREE_CODE (t) == FUNCTION_DECL)
    name = xstrdup (IDENTIFIER_POINTER (DECL_NAME (t)));
  else
    return NULL;

  return name;
}

/* We've been passed a function definition - allocate and initialize a pdb_func
 * struct to represent it. */
static void
pdbout_begin_function (tree func)
{
  struct pdb_func *f = (struct pdb_func *) xmalloc (sizeof (struct pdb_func));

  f->next = funcs;
  f->name = get_tree_name (func);
  f->num = current_function_funcdef_no;
  f->public_flag = TREE_PUBLIC (func);
  f->type = find_type (TREE_TYPE (func));

  funcs = f;

  cur_func = f;
}

/* We've been passed a late global declaration, i.e. a global variable -
 * allocate a pdb_global_var struct and add it to the list of globals. */
static void
pdbout_late_global_decl (tree var)
{
  struct pdb_global_var *v;

  if (TREE_CODE (var) != VAR_DECL)
    return;

  if (!DECL_ASSEMBLER_NAME_RAW (var))
    return;

  // We take care of static variables in functions separately
  if (DECL_CONTEXT (var) && TREE_CODE (DECL_CONTEXT (var)) == FUNCTION_DECL)
    return;

  if (!TREE_ASM_WRITTEN (var) || DECL_IGNORED_P (var))
    return;

  v = (struct pdb_global_var *) xmalloc (sizeof (struct pdb_global_var));

  v->next = global_vars;
  v->name = xstrdup (IDENTIFIER_POINTER (DECL_NAME (var)));
  v->asm_name = xstrdup (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME_RAW (var)));
  v->public_flag = TREE_PUBLIC (var);
  v->type = find_type (TREE_TYPE (var));

  global_vars = v;
}

inline hashval_t
pdb_type_tree_hasher::hash (pdb_type_tree_hasher::compare_type tree)
{
  return htab_hash_pointer (tree);
}

inline bool
pdb_type_tree_hasher::equal (const value_type type, compare_type tree)
{
  return type->tree == tree;
}

static struct pdb_type *
add_builtin_type (tree t, uint16_t id)
{
  struct pdb_type *type, **slot;

  type = (struct pdb_type *) xmalloc (offsetof (struct pdb_type, data));
  type->cv_type = 0;
  type->tree = t;
  type->next = type->next2 = NULL;
  type->id = id;

  if (last_type)
    last_type->next = type;
  else
    types = type;

  last_type = type;

  if (t)
    {
      slot =
	tree_hash_table.find_slot_with_hash (t, htab_hash_pointer (t),
					     INSERT);
      *slot = type;
    }

  return type;
}

/* Initialize the builtin types, ones that we won't output: the integers,
 * the floats, the bools, etc. Pointers to these are also counted as
 * predefined types, but we take care of these in number_types. */
static void
add_builtin_types (void)
{
  add_builtin_type (char_type_node, CV_BUILTIN_TYPE_NARROW_CHARACTER);
  add_builtin_type (signed_char_type_node, CV_BUILTIN_TYPE_SIGNED_CHARACTER);
  add_builtin_type (unsigned_char_type_node,
		    CV_BUILTIN_TYPE_UNSIGNED_CHARACTER);
  add_builtin_type (short_integer_type_node, CV_BUILTIN_TYPE_INT16SHORT);
  add_builtin_type (short_unsigned_type_node, CV_BUILTIN_TYPE_UINT16SHORT);
  long_type =
    add_builtin_type (long_integer_type_node, CV_BUILTIN_TYPE_INT32LONG);
  ulong_type =
    add_builtin_type (long_unsigned_type_node, CV_BUILTIN_TYPE_UINT32LONG);
  add_builtin_type (long_long_integer_type_node, CV_BUILTIN_TYPE_INT64QUAD);
  add_builtin_type (long_long_unsigned_type_node, CV_BUILTIN_TYPE_UINT64QUAD);

  byte_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_BYTE);
  signed_byte_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_SBYTE);
  wchar_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_WIDE_CHARACTER);
  char16_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_CHARACTER16);
  uint16_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_UINT16);
  int16_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_INT16);
  char32_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_CHARACTER32);
  uint32_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_UINT32);
  int32_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_INT32);
  uint64_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_UINT64);
  int64_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_INT64);
  uint128_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_UINT128);
  int128_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_INT128);
  hresult_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_HRESULT);

  float16_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_FLOAT16);
  float32_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_FLOAT32);
  float48_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_FLOAT48);
  float64_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_FLOAT64);
  float80_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_FLOAT80);
  float128_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_FLOAT128);

  bool8_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_BOOLEAN8);
  bool16_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_BOOLEAN16);
  bool32_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_BOOLEAN32);
  bool64_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_BOOLEAN64);
  bool128_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_BOOLEAN128);

  complex16_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_COMPLEX16);
  complex32_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_COMPLEX32);
  complex48_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_COMPLEX48);
  complex64_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_COMPLEX64);
  complex80_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_COMPLEX80);
  complex128_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_COMPLEX128);

  void_type = add_builtin_type (NULL, CV_BUILTIN_TYPE_VOID);
  nullptr_type =
    add_builtin_type (NULL, (CV_TM_NPTR << 8) | CV_BUILTIN_TYPE_VOID);

  builtins_initialized = true;
}

/* Resolve a type t to a pdb_type struct. */
static struct pdb_type *
find_type (tree t)
{
  struct pdb_type *type;

  if (!builtins_initialized)
    add_builtin_types ();

  if (!t)
    return NULL;

  // search through existing types

  type = tree_hash_table.find_with_hash (t, pdb_type_tree_hasher::hash (t));

  if (type)
    return type;

  switch (TREE_CODE (t))
    {
    case INTEGER_TYPE:
      {
	unsigned int size;

	size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 8:
	    return TYPE_UNSIGNED (t) ? byte_type : signed_byte_type;

	  case 16:
	    if (TYPE_IDENTIFIER (t)
		&& IDENTIFIER_POINTER (TYPE_IDENTIFIER (t))
		&& !strcmp (IDENTIFIER_POINTER (TYPE_IDENTIFIER (t)),
			    "wchar_t"))
	      return wchar_type;
	    else if (TYPE_IDENTIFIER (t)
		     && IDENTIFIER_POINTER (TYPE_IDENTIFIER (t))
		     && !strcmp (IDENTIFIER_POINTER (TYPE_IDENTIFIER (t)),
				 "char16_t"))
	      return char16_type;
	    else
	      return TYPE_UNSIGNED (t) ? uint16_type : int16_type;

	  case 32:
	    if (TYPE_IDENTIFIER (t)
		&& IDENTIFIER_POINTER (TYPE_IDENTIFIER (t))
		&& !strcmp (IDENTIFIER_POINTER (TYPE_IDENTIFIER (t)),
			    "char32_t"))
	      return char32_type;
	    else
	      return TYPE_UNSIGNED (t) ? uint32_type : int32_type;

	  case 64:
	    return TYPE_UNSIGNED (t) ? uint64_type : int64_type;

	  case 128:
	    return TYPE_UNSIGNED (t) ? uint128_type : int128_type;

	  default:
	    return NULL;
	  }
      }

    case REAL_TYPE:
      {
	unsigned int size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 16:
	    return float16_type;

	  case 32:
	    return float32_type;

	  case 48:
	    return float48_type;

	  case 64:
	    return float64_type;

	  case 80:
	    return float80_type;

	  case 128:
	    return float128_type;

	  default:
	    return NULL;
	  }
      }

    case BOOLEAN_TYPE:
      {
	unsigned int size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 8:
	    return bool8_type;

	  case 16:
	    return bool16_type;

	  case 32:
	    return bool32_type;

	  case 64:
	    return bool64_type;

	  case 128:
	    return bool128_type;

	  default:
	    return NULL;
	  }
      }

    case COMPLEX_TYPE:
      {
	unsigned int size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 16:
	    return complex16_type;

	  case 32:
	    return complex32_type;

	  case 48:
	    return complex48_type;

	  case 64:
	    return complex64_type;

	  case 80:
	    return complex80_type;

	  case 128:
	    return complex128_type;

	  default:
	    return NULL;
	  }
      }

    case VOID_TYPE:
      return void_type;

    case NULLPTR_TYPE:
      return nullptr_type;

    default:
      break;
    }

  if (TYPE_MAIN_VARIANT (t) != t)
    {
      type =
	tree_hash_table.find_with_hash (TYPE_MAIN_VARIANT (t),
					pdb_type_tree_hasher::
					hash (TYPE_MAIN_VARIANT (t)));

      if (type)
	return type;
    }

    return NULL;
}
