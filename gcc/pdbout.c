/* Output CodeView debugging information from GNU compiler.
 * Copyright (C) 2020 Mark Harmstone
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

#define FUNC_BEGIN_LABEL	".startfunc"
#define FUNC_END_LABEL		".endfunc"

static void pdbout_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
				   unsigned int column ATTRIBUTE_UNUSED,
				   const char *file ATTRIBUTE_UNUSED);
static void pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
				 const char *file ATTRIBUTE_UNUSED);
static void pdbout_finish (const char *filename);
static void pdbout_begin_function (tree func);
static void pdbout_late_global_decl (tree var);

static uint16_t find_type (tree t);

static struct pdb_func *funcs = NULL, *cur_func = NULL;
static struct pdb_global_var *global_vars = NULL;

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
  size_t name_len = strlen (func->name);
  uint16_t len = 40 + name_len, align;

  // start procedure

  if (len % 4 != 0)
    {
      align = 4 - (len % 4);
      len += 4 - (len % 4);
    }
  else
    align = 0;

  fprintf (asm_out_file, ".cvprocstart%u:\n", func->num);
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   (uint16_t) (len - sizeof (uint16_t)));	// reclen
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   func->public_flag ? S_GPROC32 : S_LPROC32);
  fprintf (asm_out_file, "\t.long\t0\n");	// pParent
  fprintf (asm_out_file, "\t.long\t[.cvprocend%u]-[.debug$S]\n",
	   func->num);	// pEnd
  fprintf (asm_out_file, "\t.long\t0\n");	// pNext
  fprintf (asm_out_file,
	   "\t.long\t[" FUNC_END_LABEL "%u]-[" FUNC_BEGIN_LABEL "%u]\n",
	   func->num, func->num);	// len
  fprintf (asm_out_file, "\t.long\t0\n");	// DbgStart
  fprintf (asm_out_file, "\t.long\t0\n");	// DbgEnd
  fprintf (asm_out_file, "\t.short\t0x%x\n", func->type);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
  fprintf (asm_out_file, "\t.long\t[" FUNC_BEGIN_LABEL "%u]\n",
	   func->num);	// off

  // section (will get set by the linker)
  fprintf (asm_out_file, "\t.short\t0\n");

  fprintf (asm_out_file, "\t.byte\t0\n");	// flags
  ASM_OUTPUT_ASCII (asm_out_file, func->name, name_len + 1);

  for (unsigned int i = 0; i < align; i++)
    {
      fprintf (asm_out_file, "\t.byte\t0\n");
    }

  // end procedure

  fprintf (asm_out_file, ".cvprocend%u:\n", func->num);

  fprintf (asm_out_file, "\t.short\t0x2\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", S_END);
}

/* Output DATASYM32 structure, describing a global variable: either
 * one with file-level scope (S_LDATA32) or global scope (S_GDATA32). */
static void
pdbout_ldata32 (struct pdb_global_var *v)
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
  fprintf (asm_out_file, "\t.short\t0x%x\n", v->type);
  fprintf (asm_out_file, "\t.short\t0\n");

  fprintf (asm_out_file, "\t.long\t[");	// off
  ASM_OUTPUT_LABELREF (asm_out_file, v->asm_name);
  fprintf (asm_out_file, "]\n");

  // section (will get set by the linker)
  fprintf (asm_out_file, "\t.short\t0\n");
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
  fprintf (asm_out_file, "\t.long\t[.symend]-[.symstart]\n");

  fprintf (asm_out_file, ".symstart:\n");

  while (global_vars)
    {
      struct pdb_global_var *n;

      pdbout_ldata32 (global_vars);

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

  fprintf (asm_out_file, ".symend:\n");

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
 * to the ASM file. */
static void
pdbout_finish (const char *filename ATTRIBUTE_UNUSED)
{
  write_pdb_section ();
}

/* We've been passed a function definition - allocate and initialize a pdb_func
 * struct to represent it. */
static void
pdbout_begin_function (tree func)
{
  struct pdb_func *f = (struct pdb_func *) xmalloc (sizeof (struct pdb_func));

  f->next = funcs;
  f->name = xstrdup (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (func)));
  f->num = current_function_funcdef_no;
  f->public_flag = TREE_PUBLIC (func);
  f->type = find_type (TREE_TYPE (func));

  funcs = f;

  cur_func = f;
}

/* We've been passed a late global declaration, i.e. a global function -
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

  if (!TREE_ASM_WRITTEN (var))
    return;

  v = (struct pdb_global_var *) xmalloc (sizeof (struct pdb_global_var));

  v->next = global_vars;
  v->name = xstrdup (IDENTIFIER_POINTER (DECL_NAME (var)));
  v->asm_name = xstrdup (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME_RAW (var)));
  v->public_flag = TREE_PUBLIC (var);
  v->type = find_type (TREE_TYPE (var));

  global_vars = v;
}

/* Resolve a type t to a type number. If it's a builtin type, such as bool or
 * the various ints, return its constant. */
static uint16_t
find_type (tree t)
{
  switch (TREE_CODE (t))
    {
    case INTEGER_TYPE:
      {
	unsigned int size;

	if (t == char_type_node)
	  return CV_BUILTIN_TYPE_NARROW_CHARACTER;
	else if (t == signed_char_type_node)
	  return CV_BUILTIN_TYPE_SIGNED_CHARACTER;
	else if (t == unsigned_char_type_node)
	  return CV_BUILTIN_TYPE_UNSIGNED_CHARACTER;
	else if (t == short_integer_type_node)
	  return CV_BUILTIN_TYPE_INT16SHORT;
	else if (t == short_unsigned_type_node)
	  return CV_BUILTIN_TYPE_UINT16SHORT;
	else if (t == long_integer_type_node)
	  return CV_BUILTIN_TYPE_INT32LONG;
	else if (t == long_unsigned_type_node)
	  return CV_BUILTIN_TYPE_UINT32LONG;
	else if (t == long_long_integer_type_node)
	  return CV_BUILTIN_TYPE_INT64QUAD;
	else if (t == long_long_unsigned_type_node)
	  return CV_BUILTIN_TYPE_UINT64QUAD;

	size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 8:
	    return TYPE_UNSIGNED (t) ? CV_BUILTIN_TYPE_BYTE :
	      CV_BUILTIN_TYPE_SBYTE;

	  case 16:
	    if (TYPE_IDENTIFIER (t)
		&& IDENTIFIER_POINTER (TYPE_IDENTIFIER (t))
		&& !strcmp (IDENTIFIER_POINTER (TYPE_IDENTIFIER (t)),
			    "wchar_t"))
	      return CV_BUILTIN_TYPE_WIDE_CHARACTER;
	    else if (TYPE_IDENTIFIER (t)
		     && IDENTIFIER_POINTER (TYPE_IDENTIFIER (t))
		     && !strcmp (IDENTIFIER_POINTER (TYPE_IDENTIFIER (t)),
				 "char16_t"))
	      return CV_BUILTIN_TYPE_CHARACTER16;
	    else
	      return TYPE_UNSIGNED (t) ? CV_BUILTIN_TYPE_UINT16 :
		CV_BUILTIN_TYPE_INT16;

	  case 32:
	    if (TYPE_IDENTIFIER (t)
		&& IDENTIFIER_POINTER (TYPE_IDENTIFIER (t))
		&& !strcmp (IDENTIFIER_POINTER (TYPE_IDENTIFIER (t)),
			    "char32_t"))
	      return CV_BUILTIN_TYPE_CHARACTER32;
	    else
	      return TYPE_UNSIGNED (t) ? CV_BUILTIN_TYPE_UINT32 :
		CV_BUILTIN_TYPE_INT32;

	  case 64:
	    return TYPE_UNSIGNED (t) ? CV_BUILTIN_TYPE_UINT64 :
	      CV_BUILTIN_TYPE_INT64;

	  case 128:
	    return TYPE_UNSIGNED (t) ? CV_BUILTIN_TYPE_UINT128 :
	      CV_BUILTIN_TYPE_INT128;
	  }

	return 0;
      }

    case REAL_TYPE:
      {
	unsigned int size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 16:
	    return CV_BUILTIN_TYPE_FLOAT16;

	  case 32:
	    return CV_BUILTIN_TYPE_FLOAT32;

	  case 48:
	    return CV_BUILTIN_TYPE_FLOAT48;

	  case 64:
	    return CV_BUILTIN_TYPE_FLOAT64;

	  case 80:
	    return CV_BUILTIN_TYPE_FLOAT80;

	  case 128:
	    return CV_BUILTIN_TYPE_FLOAT128;
	  }

	return 0;
      }

    case BOOLEAN_TYPE:
      {
	unsigned int size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 8:
	    return CV_BUILTIN_TYPE_BOOLEAN8;

	  case 16:
	    return CV_BUILTIN_TYPE_BOOLEAN16;

	  case 32:
	    return CV_BUILTIN_TYPE_BOOLEAN32;

	  case 64:
	    return CV_BUILTIN_TYPE_BOOLEAN64;

	  case 128:
	    return CV_BUILTIN_TYPE_BOOLEAN128;
	  }

	return 0;
      }

    case COMPLEX_TYPE:
      {
	unsigned int size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0);

	switch (size)
	  {
	  case 16:
	    return CV_BUILTIN_TYPE_COMPLEX16;

	  case 32:
	    return CV_BUILTIN_TYPE_COMPLEX32;

	  case 48:
	    return CV_BUILTIN_TYPE_COMPLEX48;

	  case 64:
	    return CV_BUILTIN_TYPE_COMPLEX64;

	  case 80:
	    return CV_BUILTIN_TYPE_COMPLEX80;

	  case 128:
	    return CV_BUILTIN_TYPE_COMPLEX128;
	  }

	return 0;
      }

    case VOID_TYPE:
      return CV_BUILTIN_TYPE_VOID;

    case NULLPTR_TYPE:
      return (CV_TM_NPTR << 8) | CV_BUILTIN_TYPE_VOID;

    default:
      break;
    }

  return 0;
}
