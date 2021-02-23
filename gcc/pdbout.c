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
#include "config/i386/i386-protos.h"
#include "md5.h"
#include "rtl.h"
#include "insn-config.h"
#include "reload.h"
#include "cp/cp-tree.h"
#include "common/common-target.h"
#include "except.h"

#define FUNC_BEGIN_LABEL	".Lstartfunc"
#define FUNC_END_LABEL		".Lendfunc"

#define FIRST_TYPE_NUM		0x1000

static void pdbout_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
				   unsigned int column ATTRIBUTE_UNUSED,
				   const char *file ATTRIBUTE_UNUSED);
static void pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
				 const char *file ATTRIBUTE_UNUSED);
static void pdbout_init (const char *filename);
static void pdbout_finish (const char *filename);
static void pdbout_begin_function (tree func);
static void pdbout_late_global_decl (tree var);
static void pdbout_start_source_file (unsigned int line ATTRIBUTE_UNUSED,
				      const char *file);
static void pdbout_source_line (unsigned int line,
				unsigned int column ATTRIBUTE_UNUSED,
				const char *text,
				int discriminator ATTRIBUTE_UNUSED,
				bool is_stmt ATTRIBUTE_UNUSED);
static void pdbout_function_decl (tree decl);
static void pdbout_var_location (rtx_insn * loc_note);
static void pdbout_begin_block (unsigned int line ATTRIBUTE_UNUSED,
				unsigned int blocknum);
static void pdbout_end_block (unsigned int line ATTRIBUTE_UNUSED,
			      unsigned int blocknum);

static struct pdb_type *find_type (tree t);

static struct pdb_func *funcs = NULL, *cur_func = NULL;
static struct pdb_block *cur_block = NULL;
static struct pdb_global_var *global_vars = NULL;
static struct pdb_type *types = NULL, *last_type = NULL;
static struct pdb_type *arglist_types = NULL;
static struct pdb_type *pointer_types = NULL;
static struct pdb_type *proc_types = NULL;
static struct pdb_type *modifier_types = NULL;
static struct pdb_type *array_types = NULL;
static struct pdb_source_file *source_files = NULL, *last_source_file = NULL;
static uint32_t source_file_string_offset = 1;
static unsigned int num_line_number_entries = 0;
static unsigned int num_source_files = 0;
static unsigned int var_loc_number = 1;
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
  pdbout_init,
  pdbout_finish,
  debug_nothing_charstar,	/* early_finish */
  debug_nothing_void,		/* assembly_start */
  debug_nothing_int_charstar,	/* define */
  debug_nothing_int_charstar,	/* undef */
  pdbout_start_source_file,
  debug_nothing_int,		/* end_source_file */
  pdbout_begin_block,
  pdbout_end_block,
  debug_true_const_tree,	/* ignore_block */
  pdbout_source_line,
  pdbout_begin_prologue,
  debug_nothing_int_charstar,	/* end_prologue */
  debug_nothing_int_charstar,	/* begin_epilogue */
  pdbout_end_epilogue,
  pdbout_begin_function,
  debug_nothing_int,		/* end_function */
  debug_nothing_tree,		/* register_main_translation_unit */
  pdbout_function_decl,
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
  pdbout_var_location,
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

/* Output DEFRANGESYMREGISTER or DEFRANGESYMREGISTERREL structure, describing
 * the scope range, register, and offset at which a local variable can be
 * found. */
static void
write_var_location (struct pdb_var_location *var_loc,
		    unsigned int next_var_loc_number, unsigned int func_num)
{
  switch (var_loc->type)
    {
    case pdb_var_loc_register:
      fprintf (asm_out_file, "\t.short\t0xe\n");
      fprintf (asm_out_file, "\t.short\t0x%x\n", S_DEFRANGE_REGISTER);
      fprintf (asm_out_file, "\t.short\t0x%x\n", var_loc->reg);
      fprintf (asm_out_file, "\t.short\t0\n");	// range attr
      fprintf (asm_out_file, "\t.secrel32\t.Lvarloc%u\n",
	       var_loc->var_loc_number);
      fprintf (asm_out_file, "\t.secidx\t.Lvarloc%u\n",
	       var_loc->var_loc_number);

      if (next_var_loc_number != 0)
	{
	  fprintf (asm_out_file, "\t.short\t[.Lvarloc%u]-[.Lvarloc%u]\n",
		   next_var_loc_number, var_loc->var_loc_number);
	}
      else
	{
	  fprintf (asm_out_file,
		   "\t.short\t[" FUNC_END_LABEL "%u]-[.Lvarloc%u]\n",
		   func_num, var_loc->var_loc_number);	// to end of function
	}

      break;

    case pdb_var_loc_regrel:
      fprintf (asm_out_file, "\t.short\t0x12\n");
      fprintf (asm_out_file, "\t.short\t0x%x\n", S_DEFRANGE_REGISTER_REL);
      fprintf (asm_out_file, "\t.short\t0x%x\n", var_loc->reg);

      // spilledUdtMember, padding, offsetParent
      fprintf (asm_out_file, "\t.short\t0\n");

      fprintf (asm_out_file, "\t.long\t0x%x\n", var_loc->offset);
      fprintf (asm_out_file, "\t.secrel32\t.Lvarloc%u\n",
	       var_loc->var_loc_number);
      fprintf (asm_out_file, "\t.secidx\t.Lvarloc%u\n",
	       var_loc->var_loc_number);

      if (next_var_loc_number != 0)
	{
	  fprintf (asm_out_file, "\t.short\t[.Lvarloc%u]-[.Lvarloc%u]\n",
		   next_var_loc_number, var_loc->var_loc_number);
	}
      else
	{
	  fprintf (asm_out_file,
		   "\t.short\t[" FUNC_END_LABEL "%u]-[.Lvarloc%u]\n",
		   func_num, var_loc->var_loc_number);	// to end of function
	}

      break;

    case pdb_var_loc_unknown:
      break;
    }
}

/* We have encountered an optimized local variable, i.e. one which doesn't
 * live in the same place for the duration of a function.
 * Output a LOCALSYM struct. */
static void
pdbout_optimized_local_variable (struct pdb_local_var *v,
				 struct pdb_var_location *var_locs,
				 unsigned int func_num)
{
  uint16_t len, align;
  size_t name_len = strlen (v->name);
  struct pdb_var_location *last_pvl = var_locs, *pvl;

  len = 11 + name_len;

  if (len % 4 != 0)
    {
      align = 4 - (len % 4);
      len += 4 - (len % 4);
    }
  else
    align = 0;

  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   (uint16_t) (len - sizeof (uint16_t)));
  fprintf (asm_out_file, "\t.short\t0x%x\n", S_LOCAL);
  fprintf (asm_out_file, "\t.long\t0x%x\n", v->type ? v->type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// flags

  ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);

  for (unsigned int i = 0; i < align; i++)
    {
      fprintf (asm_out_file, "\t.byte\t0\n");
    }

  pvl = var_locs->next;
  while (pvl)
    {
      if (pvl->var == v->t)
	{
	  write_var_location (last_pvl, pvl->var_loc_number, func_num);
	  last_pvl = pvl;
	}

      pvl = pvl->next;
    }

  write_var_location (last_pvl, 0, func_num);
}

/* Output the information as to where to a local variable can be found. */
static void
pdbout_local_variable (struct pdb_local_var *v,
		       struct pdb_var_location *var_locs,
		       unsigned int func_num)
{
  uint16_t len, align;
  size_t name_len = strlen (v->name);
  struct pdb_var_location *pvl;

  pvl = var_locs;
  while (pvl)
    {
      if (pvl->var == v->t)
	{
	  pdbout_optimized_local_variable (v, pvl, func_num);
	  return;
	}

      pvl = pvl->next;
    }

  switch (v->var_type)
    {
    case pdb_local_var_regrel:
      if (v->reg == CV_X86_EBP) // ebp is a special case
	{
	  len = 13 + name_len;

	  if (len % 4 != 0)
	    {
	      align = 4 - (len % 4);
	      len += 4 - (len % 4);
	    }
	  else
	    align = 0;

	  /* Output BPRELSYM32 struct */

	  fprintf (asm_out_file, "\t.short\t0x%x\n",
		   (uint16_t) (len - sizeof (uint16_t)));	// reclen
	  fprintf (asm_out_file, "\t.short\t0x%x\n", S_BPREL32);
	  fprintf (asm_out_file, "\t.long\t0x%x\n", v->offset);
	  fprintf (asm_out_file, "\t.long\t0x%x\n",
		   v->type ? v->type->id : 0);

	  ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);
	}
      else
	{
	  len = 15 + name_len;

	  if (len % 4 != 0)
	    {
	      align = 4 - (len % 4);
	      len += 4 - (len % 4);
	    }
	  else
	    align = 0;

	  /* Output REGREL32 struct */

	  fprintf (asm_out_file, "\t.short\t0x%x\n",
		   (uint16_t) (len - sizeof (uint16_t)));	// reclen
	  fprintf (asm_out_file, "\t.short\t0x%x\n", S_REGREL32);
	  fprintf (asm_out_file, "\t.long\t0x%x\n", v->offset);
	  fprintf (asm_out_file, "\t.long\t0x%x\n",
		   v->type ? v->type->id : 0);
	  fprintf (asm_out_file, "\t.short\t0x%x\n", v->reg);

	  ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);
	}

      for (unsigned int i = 0; i < align; i++)
	{
	  fprintf (asm_out_file, "\t.byte\t0\n");
	}
      break;

    case pdb_local_var_register:
      len = 11 + name_len;

      if (len % 4 != 0)
	{
	  align = 4 - (len % 4);
	  len += 4 - (len % 4);
	}
      else
	align = 0;

      /* Output REGSYM struct */

      fprintf (asm_out_file, "\t.short\t0x%x\n",
	       (uint16_t) (len - sizeof (uint16_t)));	// reclen
      fprintf (asm_out_file, "\t.short\t0x%x\n", S_REGISTER);
      fprintf (asm_out_file, "\t.long\t0x%x\n",
	       v->type ? v->type->id : 0);
      fprintf (asm_out_file, "\t.short\t0x%x\n", v->reg);

      ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);

      for (unsigned int i = 0; i < align; i++)
	{
	  fprintf (asm_out_file, "\t.byte\t0\n");
	}
      break;

    case pdb_local_var_symbol:
      len = 15 + name_len;

      if (len % 4 != 0)
	{
	  align = 4 - (len % 4);
	  len += 4 - (len % 4);
	}
      else
	align = 0;

      /* Output DATASYM32 struct */

      fprintf (asm_out_file, "\t.short\t0x%x\n",
	       (uint16_t) (len - sizeof (uint16_t)));	// reclen
      fprintf (asm_out_file, "\t.short\t0x%x\n", S_LDATA32);
      fprintf (asm_out_file, "\t.short\t0x%x\n",
	       v->type ? v->type->id : 0);
      fprintf (asm_out_file, "\t.short\t0\n");

      fprintf (asm_out_file, "\t.secrel32\t");	// offset
      ASM_OUTPUT_LABELREF (asm_out_file, v->symbol);
      fprintf (asm_out_file, "\n");

      fprintf (asm_out_file, "\t.secidx\t");	// section
      ASM_OUTPUT_LABELREF (asm_out_file, v->symbol);
      fprintf (asm_out_file, "\n");

      ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);

      for (unsigned int i = 0; i < align; i++)
	{
	  fprintf (asm_out_file, "\t.byte\t0\n");
	}
      break;

    default:
      break;
    }
}

/* Output BLOCKSYM32 structure, describing block-level scope
 * for the purpose of local variables. */
static void
pdbout_block (struct pdb_block *block, struct pdb_func *func)
{
  struct pdb_local_var *local_var = func->local_vars;

  while (local_var)
    {
      if (local_var->block_num == block->num)
	pdbout_local_variable (local_var, func->var_locs, func->num);

      local_var = local_var->next;
    }

  while (block->children)
    {
      struct pdb_block *n = block->children->next;

      fprintf (asm_out_file, ".Lcvblockstart%u:\n", block->children->num);
      fprintf (asm_out_file, "\t.short\t0x16\n");	// reclen
      fprintf (asm_out_file, "\t.short\t0x%x\n", S_BLOCK32);

      // pParent
      if (block->num != 0)
	{
	  fprintf (asm_out_file, "\t.long\t[.Lcvblockstart%u]-[.debug$S]\n",
		   block->num);
	}
      else
	{
	  fprintf (asm_out_file, "\t.long\t[.Lcvprocstart%u]-[.debug$S]\n",
		   func->num);
	}

      fprintf (asm_out_file, "\t.long\t[.Lcvblockend%u]-[.debug$S]\n",
	       block->children->num);	// pEnd
      fprintf (asm_out_file, "\t.long\t[.Lblockend%u]-[.Lblockstart%u]\n",
	       block->children->num, block->children->num);	// length
      fprintf (asm_out_file, "\t.secrel32\t.Lblockstart%u\n",
	       block->children->num);	// offset
      fprintf (asm_out_file, "\t.secidx\t.Lblockstart%u\n",
	       block->children->num);	// offset

      fprintf (asm_out_file, "\t.byte\t0\n");	// name (zero-length string)
      fprintf (asm_out_file, "\t.byte\t0\n");	// padding

      pdbout_block (block->children, func);

      fprintf (asm_out_file, ".Lcvblockend%u:\n", block->children->num);
      fprintf (asm_out_file, "\t.short\t0x2\n");
      fprintf (asm_out_file, "\t.short\t0x%x\n", S_END);

      free (block->children);

      block->children = n;
    }
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

  pdbout_block (&func->block, func);

  // end procedure

  fprintf (asm_out_file, ".Lcvprocend%u:\n", func->num);

  fprintf (asm_out_file, "\t.short\t0x2\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", S_END);

  while (func->local_vars)
    {
      struct pdb_local_var *n = func->local_vars->next;

      if (func->local_vars->symbol)
	free (func->local_vars->symbol);

      free (func->local_vars);

      func->local_vars = n;
    }

  while (func->var_locs)
    {
      struct pdb_var_location *n = func->var_locs->next;

      free (func->var_locs);

      func->var_locs = n;
    }
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

/* Output names of the files which make up this translation unit,
 * along with their MD5 checksums. */
static void
write_file_checksums ()
{
  fprintf (asm_out_file, "\t.long\t0x%x\n", DEBUG_S_FILECHKSMS);
  fprintf (asm_out_file, "\t.long\t[.Lchksumsend]-[.Lchksumsstart]\n");
  fprintf (asm_out_file, ".Lchksumsstart:\n");

  while (source_files)
    {
      struct pdb_source_file *n;

      fprintf (asm_out_file, "\t.long\t0x%x\n", source_files->str_offset);
      fprintf (asm_out_file, "\t.byte\t0x%x\n", 16);	// length of MD5 hash
      fprintf (asm_out_file, "\t.byte\t0x%x\n", CHKSUM_TYPE_MD5);

      for (unsigned int i = 0; i < 16; i++)
	{
	  fprintf (asm_out_file, "\t.byte\t0x%x\n", source_files->hash[i]);
	}

      fprintf (asm_out_file, "\t.short\t0\n");

      n = source_files->next;

      free (source_files);

      source_files = n;
    }

  fprintf (asm_out_file, ".Lchksumsend:\n");
}

/* Loop through each function, and output the line number to
 * address mapping. */
static void
write_line_numbers ()
{
  struct pdb_func *func = funcs;
  unsigned int lines_part = 0;

  while (func)
    {
      while (func->lines)
	{
	  struct pdb_line *l, *last_line;
	  unsigned int num_entries = 0, source_file, first_entry;

	  source_file = func->lines->source_file;

	  l = last_line = func->lines;
	  while (l && l->source_file == source_file)
	    {
	      num_entries++;
	      last_line = l;
	      l = l->next;
	    }

	  first_entry = func->lines->entry;

	  fprintf (asm_out_file, "\t.long\t0x%x\n", DEBUG_S_LINES);
	  fprintf (asm_out_file, "\t.long\t[.Llinesend%u]-[.Llinesstart%u]\n",
		   lines_part, lines_part);
	  fprintf (asm_out_file, ".Llinesstart%u:\n", lines_part);

	  // offset
	  fprintf (asm_out_file, "\t.secrel32\t.Lline%u\n", first_entry);
	  // section
	  fprintf (asm_out_file, "\t.secidx\t.Lline%u\n", first_entry);

	  fprintf (asm_out_file, "\t.short\t0\n");	// flags

	  // next section of function is another source file
	  if (last_line->next)
	    {
	      fprintf (asm_out_file, "\t.long\t[.Lline%u]-[.Lline%u]\n",
		       last_line->next->entry, first_entry);	// length
	    }
	  else
	    {
	      fprintf (asm_out_file,
		       "\t.long\t[" FUNC_END_LABEL "%u]-[.Lline%u]\n",
		       func->num, first_entry);	// length
	    }

	  // file ID (0x18 is size of checksum struct)
	  fprintf (asm_out_file, "\t.long\t0x%x\n", source_file * 0x18);
	  fprintf (asm_out_file, "\t.long\t0x%x\n", num_entries);
	  // length of file block
	  fprintf (asm_out_file, "\t.long\t0x%x\n", 0xc + (num_entries * 8));

	  while (func->lines && func->lines->source_file == source_file)
	    {
	      struct pdb_line *n = func->lines->next;

	      // offset
	      fprintf (asm_out_file, "\t.long\t[.Lline%u]-[.Lline%u]\n",
		       func->lines->entry, first_entry);

	      // line no.
	      fprintf (asm_out_file, "\t.long\t0x%x\n", func->lines->line);

	      free (func->lines);

	      func->lines = n;
	    }

	  fprintf (asm_out_file, ".Llinesend%u:\n", lines_part);
	  lines_part++;
	}

      func = func->next;
    }
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
  struct pdb_source_file *psf;
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

  fprintf (asm_out_file, "\t.long\t0x%x\n", DEBUG_S_STRINGTABLE);
  fprintf (asm_out_file, "\t.long\t[.Lstrtableend]-[.Lstrtablestart]\n");
  fprintf (asm_out_file, ".Lstrtablestart:\n");
  fprintf (asm_out_file, "\t.byte\t0\n");

  psf = source_files;
  while (psf)
    {
      size_t name_len = strlen (psf->name);

      ASM_OUTPUT_ASCII (asm_out_file, psf->name + name_len + 1,
			strlen (psf->name + name_len + 1) + 1);

      psf = psf->next;
    }

  fprintf (asm_out_file, "\t.balign\t4\n");
  fprintf (asm_out_file, ".Lstrtableend:\n");

  write_file_checksums ();

  write_line_numbers ();

  while (funcs)
    {
      struct pdb_func *n = funcs->next;

      if (funcs->name)
	free (funcs->name);

      free (funcs);

      funcs = n;
    }
}

/* Free a pdb_type that we've allocated. */
static void
free_type (struct pdb_type *t)
{
  free (t);
}

/* Output a lfPointer structure. */
static void
write_pointer (struct pdb_pointer *ptr)
{
  fprintf (asm_out_file, "\t.short\t0xa\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_POINTER);
  fprintf (asm_out_file, "\t.short\t0x%x\n", ptr->type ? ptr->type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
  fprintf (asm_out_file, "\t.long\t0x%x\n", ptr->attr.num);
}

/* Output a lfArray structure. */
static void
write_array (struct pdb_array *arr)
{
  uint16_t len = 15, align;

  if (arr->length >= 0x8000)
    {
      if (arr->length <= 0xffff)
	len += 2;		// LF_USHORT
      else if (arr->length <= 0xffffffff)
	len += 4;		// LF_ULONG
      else
	len += 8;		// LF_UQUADWORD
    }

  align = 4 - (len % 4);

  if (align != 4)
    len += align;

  fprintf (asm_out_file, "\t.short\t0x%lx\n", len - sizeof (uint16_t));
  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_ARRAY);

  fprintf (asm_out_file, "\t.short\t0x%x\n", arr->type ? arr->type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   arr->index_type ? arr->index_type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding

  if (arr->length >= 0x8000)
    {
      if (arr->length <= 0xffff)
	{
	  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_USHORT);
	  fprintf (asm_out_file, "\t.short\t0x%x\n", (uint16_t) arr->length);
	}
      else if (arr->length <= 0xffffffff)
	{
	  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_ULONG);
	  fprintf (asm_out_file, "\t.long\t0x%x\n", (uint32_t) arr->length);
	}
      else
	{
	  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_UQUADWORD);
	  fprintf (asm_out_file, "\t.quad\t0x%" PRIx64 "\n", arr->length);
	}
    }
  else
    fprintf (asm_out_file, "\t.short\t0x%x\n", (uint32_t) arr->length);

  fprintf (asm_out_file, "\t.byte\t0\n");	// empty string

  if (align != 4)
    {
      if (align == 3)
	fprintf (asm_out_file, "\t.byte\t0xf3\n");

      if (align >= 2)
	fprintf (asm_out_file, "\t.byte\t0xf2\n");

      fprintf (asm_out_file, "\t.byte\t0xf1\n");
    }
}

/* Output a lfArgList structure, describing the arguments that a
 * procedure expects. */
static void
write_arglist (struct pdb_arglist *arglist)
{
  unsigned int len = 8 + (4 * arglist->count);

  if (arglist->count == 0)	// zero-length arglist has dummy entry
    len += 4;

  fprintf (asm_out_file, "\t.short\t0x%x\n", len - 2);
  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_ARGLIST);
  fprintf (asm_out_file, "\t.long\t0x%x\n",
	   arglist->count == 0 ? 1 : arglist->count);

  for (unsigned int i = 0; i < arglist->count; i++)
    {
      fprintf (asm_out_file, "\t.short\t0x%x\n",
	       arglist->args[i] ? arglist->args[i]->id : 0);
      fprintf (asm_out_file, "\t.short\t0\n");	// padding
    }

  if (arglist->count == 0)
    {
      fprintf (asm_out_file, "\t.short\t0\n");	// empty type
      fprintf (asm_out_file, "\t.short\t0\n");	// padding
    }
}

/* Output a lfProc structure, which describes the prototype of a
 * procedure. See also pdbout_proc32, which outputs the details of
 * a specific procedure. */
static void
write_procedure (struct pdb_proc *proc)
{
  fprintf (asm_out_file, "\t.short\t0xe\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_PROCEDURE);
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   proc->return_type ? proc->return_type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
  fprintf (asm_out_file, "\t.byte\t0x%x\n", proc->calling_convention);
  fprintf (asm_out_file, "\t.byte\t0x%x\n", proc->attributes);
  fprintf (asm_out_file, "\t.short\t0x%x\n", proc->num_args);
  fprintf (asm_out_file, "\t.short\t0x%x\n",
	   proc->arg_list ? proc->arg_list->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
}

/* Output lfModifier structure, representing a const or volatile version
 * of an existing type. */
static void
write_modifier (struct pdb_modifier *t)
{
  fprintf (asm_out_file, "\t.short\t0xa\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", LF_MODIFIER);
  fprintf (asm_out_file, "\t.short\t0x%x\n", t->type ? t->type->id : 0);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
  fprintf (asm_out_file, "\t.short\t0x%x\n", t->modifier);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
}

/* Given a pdb_type, output its definition. */
static void
write_type (struct pdb_type *t)
{
  switch (t->cv_type)
    {
    case LF_POINTER:
      if (t->id < FIRST_TYPE_NUM)	// pointer to builtin
	return;

      write_pointer ((struct pdb_pointer *) t->data);
      break;

    case LF_ARRAY:
      write_array ((struct pdb_array *) t->data);
      break;

    case LF_ARGLIST:
      write_arglist ((struct pdb_arglist *) t->data);
      break;

    case LF_PROCEDURE:
      write_procedure ((struct pdb_proc *) t->data);
      break;

    case LF_MODIFIER:
      write_modifier ((struct pdb_modifier *) t->data);
      break;
    }
}

/* Output the .debug$T section, which contains all the types used. */
static void
write_pdb_type_section (void)
{
  struct pdb_type *n;

  fprintf (asm_out_file, "\t.section\t.debug$T, \"ndr\"\n");
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_SIGNATURE_C13);

  n = types;
  while (n)
    {
      write_type (n);

      n = n->next;
    }

  while (types)
    {
      n = types->next;

      free_type (types);

      types = n;
    }
}

/* Loop through our types and assign them sequential numbers. */
static void
number_types (void)
{
  struct pdb_type *t;
  uint16_t type_num = FIRST_TYPE_NUM;

  t = types;
  while (t)
    {
      if (t->id != 0)
	{
	  t = t->next;
	  continue;
	}

      switch (t->cv_type)
	{
	case LF_POINTER:
	  {
	    struct pdb_pointer *ptr = (struct pdb_pointer *) t->data;

	    // pointers to builtins have their own constants
	    if (ptr->type && ptr->type->id != 0 && ptr->type->id < 0x100)
	      {
		if (ptr->attr.s.ptrtype == CV_PTR_NEAR32)
		  {
		    t->id = (CV_TM_NPTR32 << 8) | ptr->type->id;
		    break;
		  }
		else if (ptr->attr.s.ptrtype == CV_PTR_64)
		  {
		    t->id = (CV_TM_NPTR64 << 8) | ptr->type->id;
		    break;
		  }
	      }
	    [[fallthrough]];
	  }

	default:
	  t->id = type_num;
	  type_num++;

	  if (type_num == 0)	// overflow
	    {
	      fprintf (stderr, "too many CodeView types\n");
	      xexit (1);
	    }
	}

      t = t->next;
    }
}

/* We've finished compilation - output the .debug$S and .debug$T sections
 * to the asm file. */
static void
pdbout_finish (const char *filename ATTRIBUTE_UNUSED)
{
  number_types ();

  write_pdb_section ();
  write_pdb_type_section ();
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
  expanded_location xloc;
  struct pdb_func *f = (struct pdb_func *) xmalloc (sizeof (struct pdb_func));

  f->next = funcs;
  f->name = get_tree_name (func);
  f->num = current_function_funcdef_no;
  f->public_flag = TREE_PUBLIC (func);
  f->type = find_type (TREE_TYPE (func));
  f->lines = f->last_line = NULL;
  f->local_vars = f->last_local_var = NULL;
  f->var_locs = f->last_var_loc = NULL;

  f->block.next = NULL;
  f->block.parent = NULL;
  f->block.num = 0;
  f->block.children = f->block.last_child = NULL;

  funcs = f;

  cur_func = f;
  cur_block = &f->block;

  xloc = expand_location (DECL_SOURCE_LOCATION (func));

  if (xloc.line != 0)
    pdbout_source_line (xloc.line, 0, xloc.file, 0, 0);
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

/* Given an array type t, allocate a new pdb_type and add it to the
 * type list. */
static struct pdb_type *
find_type_array (tree t)
{
  struct pdb_type *arrtype, *last_entry = NULL, *type;
  struct pdb_array *arr;
  uint64_t length =
    TYPE_SIZE (t) ? (TREE_INT_CST_ELT (TYPE_SIZE (t), 0) / 8) : 0;
  struct pdb_type **slot;

  type = find_type (TREE_TYPE (t));

  if (!type)
    return NULL;

  arrtype = array_types;
  while (arrtype)
    {
      arr = (struct pdb_array *) arrtype->data;

      if (arr->type == type && arr->length == length)
	return arrtype;

      last_entry = arrtype;
      arrtype = arrtype->next2;
    }

  arrtype =
    (struct pdb_type *) xmalloc (offsetof (struct pdb_type, data) +
				 sizeof (struct pdb_array));
  arrtype->cv_type = LF_ARRAY;
  arrtype->tree = t;
  arrtype->next = arrtype->next2 = NULL;
  arrtype->id = 0;

  arr = (struct pdb_array *) arrtype->data;
  arr->type = type;
  arr->index_type = ulong_type;
  arr->length = length;

  if (last_entry)
    last_entry->next2 = arrtype;
  else
    array_types = arrtype;

  if (last_type)
    last_type->next = arrtype;
  else
    types = arrtype;

  last_type = arrtype;

  slot =
    tree_hash_table.find_slot_with_hash (t, htab_hash_pointer (t), INSERT);
  *slot = arrtype;

  return arrtype;
}

/* Add an argument list type. */
static pdb_type *
add_arglist_type (struct pdb_type *t)
{
  struct pdb_type *t2 = arglist_types;
  struct pdb_type *last_entry = NULL;

  // check for dupes

  while (t2)
    {
      struct pdb_arglist *arglist1 = (struct pdb_arglist *) t->data;
      struct pdb_arglist *arglist2 = (struct pdb_arglist *) t2->data;

      if (arglist1->count == arglist2->count)
	{
	  bool same = true;

	  for (unsigned int i = 0; i < arglist1->count; i++)
	    {
	      if (arglist1->args[i] != arglist2->args[i])
		{
		  same = false;
		  break;
		}
	    }

	  if (same)
	    {
	      free (t);

	      return t2;
	    }
	}

      last_entry = t2;
      t2 = t2->next2;
    }

  // add new

  t->next = NULL;
  t->next2 = NULL;
  t->id = 0;

  if (last_type)
    last_type->next = t;
  else
    types = t;

  last_type = t;

  if (last_entry)
    last_entry->next2 = t;
  else
    arglist_types = t;

  return t;
}

/* Given a pointer type t, allocate a new pdb_type and add it to the
 * type list. */
static struct pdb_type *
find_type_pointer (tree t)
{
  struct pdb_type *ptrtype, *t2, *last_entry = NULL, *type;
  struct pdb_pointer *ptr, v;
  unsigned int size = TREE_INT_CST_ELT (TYPE_SIZE (t), 0) / 8;
  struct pdb_type **slot;

  type = find_type (TREE_TYPE (t));

  if (!type)
    return NULL;

  v.attr.num = 0;

  v.attr.s.size = size;

  if (size == 8)
    v.attr.s.ptrtype = CV_PTR_64;
  else if (size == 4)
    v.attr.s.ptrtype = CV_PTR_NEAR32;

  if (TREE_CODE (t) == REFERENCE_TYPE)
    v.attr.s.ptrmode =
      TYPE_REF_IS_RVALUE (t) ? CV_PTR_MODE_RVREF : CV_PTR_MODE_LVREF;

  t2 = pointer_types;
  while (t2)
    {
      ptr = (struct pdb_pointer *) t2->data;

      if (ptr->type == type && ptr->attr.num == v.attr.num)
	return t2;

      last_entry = t2;
      t2 = t2->next2;
    }

  ptrtype =
    (struct pdb_type *) xmalloc (offsetof (struct pdb_type, data) +
				 sizeof (struct pdb_pointer));
  ptrtype->cv_type = LF_POINTER;
  ptrtype->tree = t;
  ptrtype->next = ptrtype->next2 = NULL;
  ptrtype->id = 0;

  ptr = (struct pdb_pointer *) ptrtype->data;
  ptr->type = type;
  ptr->attr.num = v.attr.num;

  if (last_entry)
    last_entry->next2 = ptrtype;
  else
    pointer_types = ptrtype;

  if (last_type)
    last_type->next = ptrtype;
  else
    types = ptrtype;

  last_type = ptrtype;

  slot =
    tree_hash_table.find_slot_with_hash (t, htab_hash_pointer (t), INSERT);
  *slot = ptrtype;

  return ptrtype;
}

/* Given a function type t, allocate a new pdb_type and add it to the
 * type list. */
static struct pdb_type *
find_type_function (tree t)
{
  struct pdb_type *arglisttype, *proctype, *last_entry = NULL;
  struct pdb_arglist *arglist;
  struct pdb_proc *proc;
  tree arg;
  unsigned int num_args = 0;
  struct pdb_type **argptr;
  struct pdb_type *return_type;
  uint8_t calling_convention;
  struct pdb_type **slot;

  // create arglist

  arg = TYPE_ARG_TYPES (t);
  while (arg)
    {
      if (TREE_CODE (TREE_VALUE (arg)) != VOID_TYPE)
	num_args++;

      arg = TREE_CHAIN (arg);
    }

  arglisttype =
    (struct pdb_type *) xmalloc (offsetof (struct pdb_type, data) +
				 offsetof (struct pdb_arglist, args) +
				 (num_args * sizeof (struct pdb_type *)));
  arglisttype->cv_type = LF_ARGLIST;
  arglisttype->tree = NULL;

  arglist = (struct pdb_arglist *) arglisttype->data;
  arglist->count = num_args;

  argptr = arglist->args;
  arg = TYPE_ARG_TYPES (t);
  while (arg)
    {
      if (TREE_CODE (TREE_VALUE (arg)) != VOID_TYPE)
	{
	  *argptr = find_type (TREE_VALUE (arg));
	  argptr++;
	}

      arg = TREE_CHAIN (arg);
    }

  arglisttype = add_arglist_type (arglisttype);

  // create procedure

  return_type = find_type (TREE_TYPE (t));

  if (TARGET_64BIT)
    calling_convention = CV_CALL_NEAR_C;
  else
    {
      switch (ix86_get_callcvt (t))
	{
	case IX86_CALLCVT_CDECL:
	  calling_convention = CV_CALL_NEAR_C;
	  break;

	case IX86_CALLCVT_STDCALL:
	  calling_convention = CV_CALL_NEAR_STD;
	  break;

	case IX86_CALLCVT_FASTCALL:
	  calling_convention = CV_CALL_NEAR_FAST;
	  break;

	case IX86_CALLCVT_THISCALL:
	  calling_convention = CV_CALL_THISCALL;
	  break;

	default:
	  calling_convention = CV_CALL_NEAR_C;
	}
    }

  proctype = proc_types;
  while (proctype)
    {
      proc = (struct pdb_proc *) proctype->data;

      if (proc->return_type == return_type
	  && proc->calling_convention == calling_convention
	  && proc->num_args == num_args && proc->arg_list == arglisttype)
	return proctype;

      last_entry = proctype;
      proctype = proctype->next2;
    }

  proctype =
    (struct pdb_type *) xmalloc (offsetof (struct pdb_type, data) +
				 sizeof (struct pdb_proc));
  proctype->cv_type = LF_PROCEDURE;
  proctype->tree = t;
  proctype->next = proctype->next2 = NULL;
  proctype->id = 0;

  proc = (struct pdb_proc *) proctype->data;

  proc->return_type = return_type;
  proc->attributes = 0;
  proc->num_args = num_args;
  proc->arg_list = arglisttype;
  proc->calling_convention = calling_convention;

  if (last_entry)
    last_entry->next2 = proctype;
  else
    proc_types = proctype;

  if (last_type)
    last_type->next = proctype;
  else
    types = proctype;

  last_type = proctype;

  slot =
    tree_hash_table.find_slot_with_hash (t, htab_hash_pointer (t), INSERT);
  *slot = proctype;

  return proctype;
}

/* Given a CV-modified type t, allocate a new pdb_type modifying
 * the base type, and add it to the type list. */
static struct pdb_type *
find_type_modifier (tree t)
{
  struct pdb_type *type, *last_entry = NULL, *base_type;
  struct pdb_modifier *mod;
  uint16_t modifier = 0;
  struct pdb_type **slot;

  base_type = find_type (TYPE_MAIN_VARIANT (t));

  if (TYPE_READONLY (t))
    modifier |= CV_MODIFIER_CONST;

  if (TYPE_VOLATILE (t))
    modifier |= CV_MODIFIER_VOLATILE;

  type = modifier_types;
  while (type)
    {
      mod = (struct pdb_modifier *) type->data;

      if (mod->type == base_type && mod->modifier == modifier)
	return type;

      last_entry = type;
      type = type->next2;
    }

  type =
    (struct pdb_type *) xmalloc (offsetof (struct pdb_type, data) +
				 sizeof (struct pdb_modifier));
  type->cv_type = LF_MODIFIER;
  type->tree = t;
  type->next = type->next2 = NULL;
  type->id = 0;

  mod = (struct pdb_modifier *) type->data;

  mod->type = base_type;
  mod->modifier = modifier;

  if (last_entry)
    last_entry->next2 = type;
  else
    modifier_types = type;

  if (last_type)
    last_type->next = type;
  else
    types = type;

  last_type = type;

  slot =
    tree_hash_table.find_slot_with_hash (t, htab_hash_pointer (t), INSERT);
  *slot = type;

  return type;
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

  // add modifier type if const or volatile

  if (TYPE_READONLY (t) || TYPE_VOLATILE (t))
    return find_type_modifier (t);

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

  switch (TREE_CODE (t))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      return find_type_pointer (t);

    case ARRAY_TYPE:
      return find_type_array (t);

    case FUNCTION_TYPE:
    case METHOD_TYPE:
      return find_type_function (t);

    default:
      return NULL;
    }
}

#ifndef _WIN32
/* Given a Unix-style path, construct a fake Windows path, which is what windbg
 * and Visual Studio are expecting. This maps / to Z:\, which is the default
 * behaviour on Wine. */
static char *
make_windows_path (char *src)
{
  size_t len = strlen (src);
  char *dest = (char *) xmalloc (len + 3);
  char *in, *ptr;

  ptr = dest;
  *ptr = 'Z';
  ptr++;
  *ptr = ':';
  ptr++;

  in = src;

  for (unsigned int i = 0; i < len; i++)
    {
      if (*in == '/')
	*ptr = '\\';
      else
	*ptr = *in;

      in++;
      ptr++;
    }

  *ptr = 0;

  free (src);

  return dest;
}
#endif

/* Add a source file to the list of files making up this translation unit.
 * Non-Windows systems will see the filename being given a fake Windows-style
 * path, so as not to confuse Microsoft's debuggers.
 * This also includes a MD5 checksum, which MSVC uses to tell if a file has
 * been modified since compilation. Recent versions of MSVC seem to use SHA1
 * instead. */
static void
add_source_file (const char *file)
{
  struct pdb_source_file *psf;
  char *path;
  size_t file_len, path_len;
  FILE *f;

  // check not already added
  psf = source_files;
  while (psf)
    {
      if (!strcmp (psf->name, file))
	return;

      psf = psf->next;
    }

  path = lrealpath (file);
  if (!path)
    return;

#ifndef _WIN32
  path = make_windows_path (path);
#endif

  file_len = strlen (file);
  path_len = strlen (path);

  f = fopen (file, "r");

  if (!f)
    {
      free (path);
      return;
    }

  psf =
    (struct pdb_source_file *)
    xmalloc (offsetof (struct pdb_source_file, name) + file_len + 1 +
	     path_len + 1);

  md5_stream (f, psf->hash);

  fclose (f);

  psf->next = NULL;
  psf->str_offset = source_file_string_offset;
  memcpy (psf->name, file, file_len + 1);
  memcpy (psf->name + file_len + 1, path, path_len + 1);

  free (path);

  source_file_string_offset += path_len + 1;

  if (last_source_file)
    last_source_file->next = psf;

  last_source_file = psf;

  if (!source_files)
    source_files = psf;

  psf->num = num_source_files;

  num_source_files++;
}

/* We've encountered an #include - add the header file to the
 * list of source files. */
static void
pdbout_start_source_file (unsigned int line ATTRIBUTE_UNUSED,
			  const char *file)
{
  add_source_file (file);
}

/* Start of compilation - add the main source file to the list. */
static void
pdbout_init (const char *file)
{
  add_source_file (file);
}

/* We've encountered a new line of source code. Add an asm label for this,
 * and record the mapping for later. */
static void
pdbout_source_line (unsigned int line, unsigned int column ATTRIBUTE_UNUSED,
		    const char *text,
		    int discriminator ATTRIBUTE_UNUSED,
		    bool is_stmt ATTRIBUTE_UNUSED)
{
  struct pdb_line *ent;
  struct pdb_source_file *psf;
  unsigned int source_file = 0;

  if (!cur_func)
    return;

  psf = source_files;
  while (psf)
    {
      if (!strcmp (text, psf->name))
	{
	  source_file = psf->num;
	  break;
	}

      psf = psf->next;
    }

  if (cur_func->last_line && cur_func->last_line->line == line
      && cur_func->last_line->source_file == source_file)
    return;

  ent = (struct pdb_line *) xmalloc (sizeof (struct pdb_line));

  ent->next = NULL;
  ent->line = line;
  ent->entry = num_line_number_entries;
  ent->source_file = source_file;

  if (cur_func->last_line)
    cur_func->last_line->next = ent;

  cur_func->last_line = ent;

  if (!cur_func->lines)
    cur_func->lines = ent;

  fprintf (asm_out_file, ".Lline%u:\n", num_line_number_entries);

  num_line_number_entries++;
}

/* Given an x86 gcc register no., return the CodeView equivalent. */
static enum pdb_x86_register
map_register_no_x86 (unsigned int regno, machine_mode mode)
{
  switch (mode)
    {
    case E_SImode:
      switch (regno)
	{
	case AX_REG:
	  return CV_X86_EAX;

	case DX_REG:
	  return CV_X86_EDX;

	case CX_REG:
	  return CV_X86_ECX;

	case BX_REG:
	  return CV_X86_EBX;

	case SI_REG:
	  return CV_X86_ESI;

	case DI_REG:
	  return CV_X86_EDI;

	case BP_REG:
	  return CV_X86_EBP;

	case SP_REG:
	  return CV_X86_ESP;

	case FLAGS_REG:
	  return CV_X86_EFLAGS;
	}

      break;

    case E_HImode:
      switch (regno)
	{
	case AX_REG:
	  return CV_X86_AX;

	case DX_REG:
	  return CV_X86_DX;

	case CX_REG:
	  return CV_X86_CX;

	case BX_REG:
	  return CV_X86_BX;

	case SI_REG:
	  return CV_X86_SI;

	case DI_REG:
	  return CV_X86_DI;

	case BP_REG:
	  return CV_X86_BP;

	case SP_REG:
	  return CV_X86_SP;

	case FLAGS_REG:
	  return CV_X86_FLAGS;
	}

      break;

    case E_QImode:
      switch (regno)
	{
	case AX_REG:
	  return CV_X86_AL;

	case DX_REG:
	  return CV_X86_DL;

	case CX_REG:
	  return CV_X86_CL;

	case BX_REG:
	  return CV_X86_BL;
	}

      break;

    case E_SFmode:
    case E_DFmode:
      switch (regno)
	{
	case XMM0_REG:
	  return CV_X86_XMM0;

	case XMM1_REG:
	  return CV_X86_XMM1;

	case XMM2_REG:
	  return CV_X86_XMM2;

	case XMM3_REG:
	  return CV_X86_XMM3;

	case XMM4_REG:
	  return CV_X86_XMM4;

	case XMM5_REG:
	  return CV_X86_XMM5;

	case XMM6_REG:
	  return CV_X86_XMM6;

	case XMM7_REG:
	  return CV_X86_XMM7;

	case ST0_REG:
	  return CV_X86_ST0;

	case ST1_REG:
	  return CV_X86_ST1;

	case ST2_REG:
	  return CV_X86_ST2;

	case ST3_REG:
	  return CV_X86_ST3;

	case ST4_REG:
	  return CV_X86_ST4;

	case ST5_REG:
	  return CV_X86_ST5;

	case ST6_REG:
	  return CV_X86_ST6;

	case ST7_REG:
	  return CV_X86_ST7;
	}

      break;

    case E_DImode:
      /* Suppress warning for 64-bit pseudo-registers on x86, e.g. an 8-byte
       * struct put in ecx:edx. Not representible with CodeView? */
      return CV_X86_NONE;

    default:
      break;
    }

  warning (0, "could not map x86 register %u, mode %u to CodeView constant",
	   regno, mode);

  return CV_X86_NONE;
}

/* Given an amd64 gcc register no., return the CodeView equivalent. */
static enum pdb_amd64_register
map_register_no_amd64 (unsigned int regno, machine_mode mode)
{
  switch (mode)
    {
    case E_SImode:
    case E_SFmode:
    case E_SDmode:
      switch (regno)
	{
	case AX_REG:
	  return CV_AMD64_EAX;

	case DX_REG:
	  return CV_AMD64_EDX;

	case CX_REG:
	  return CV_AMD64_ECX;

	case BX_REG:
	  return CV_AMD64_EBX;

	case SI_REG:
	  return CV_AMD64_ESI;

	case DI_REG:
	  return CV_AMD64_EDI;

	case BP_REG:
	  return CV_AMD64_EBP;

	case SP_REG:
	  return CV_AMD64_ESP;

	case FLAGS_REG:
	  return CV_AMD64_EFLAGS;

	case R8_REG:
	  return CV_AMD64_R8D;

	case R9_REG:
	  return CV_AMD64_R9D;

	case R10_REG:
	  return CV_AMD64_R10D;

	case R11_REG:
	  return CV_AMD64_R11D;

	case R12_REG:
	  return CV_AMD64_R12D;

	case R13_REG:
	  return CV_AMD64_R13D;

	case R14_REG:
	  return CV_AMD64_R14D;

	case R15_REG:
	  return CV_AMD64_R15D;

	case XMM0_REG:
	  return CV_AMD64_XMM0_0;

	case XMM1_REG:
	  return CV_AMD64_XMM1_0;

	case XMM2_REG:
	  return CV_AMD64_XMM2_0;

	case XMM3_REG:
	  return CV_AMD64_XMM3_0;

	case XMM4_REG:
	  return CV_AMD64_XMM4_0;

	case XMM5_REG:
	  return CV_AMD64_XMM5_0;

	case XMM6_REG:
	  return CV_AMD64_XMM6_0;

	case XMM7_REG:
	  return CV_AMD64_XMM7_0;

	case XMM8_REG:
	  return CV_AMD64_XMM8_0;

	case XMM9_REG:
	  return CV_AMD64_XMM9_0;

	case XMM10_REG:
	  return CV_AMD64_XMM10_0;

	case XMM11_REG:
	  return CV_AMD64_XMM11_0;

	case XMM12_REG:
	  return CV_AMD64_XMM12_0;

	case XMM13_REG:
	  return CV_AMD64_XMM13_0;

	case XMM14_REG:
	  return CV_AMD64_XMM14_0;

	case XMM15_REG:
	  return CV_AMD64_XMM15_0;
	}

      break;

    case E_DImode:
    case E_DDmode:
    case E_DFmode:
      switch (regno)
	{
	case AX_REG:
	  return CV_AMD64_RAX;

	case DX_REG:
	  return CV_AMD64_RDX;

	case CX_REG:
	  return CV_AMD64_RCX;

	case BX_REG:
	  return CV_AMD64_RBX;

	case SI_REG:
	  return CV_AMD64_RSI;

	case DI_REG:
	  return CV_AMD64_RDI;

	case BP_REG:
	  return CV_AMD64_RBP;

	case SP_REG:
	  return CV_AMD64_RSP;

	case R8_REG:
	  return CV_AMD64_R8;

	case R9_REG:
	  return CV_AMD64_R9;

	case R10_REG:
	  return CV_AMD64_R10;

	case R11_REG:
	  return CV_AMD64_R11;

	case R12_REG:
	  return CV_AMD64_R12;

	case R13_REG:
	  return CV_AMD64_R13;

	case R14_REG:
	  return CV_AMD64_R14;

	case R15_REG:
	  return CV_AMD64_R15;

	case XMM0_REG:
	  return CV_AMD64_XMM0L;

	case XMM1_REG:
	  return CV_AMD64_XMM1L;

	case XMM2_REG:
	  return CV_AMD64_XMM2L;

	case XMM3_REG:
	  return CV_AMD64_XMM3L;

	case XMM4_REG:
	  return CV_AMD64_XMM4L;

	case XMM5_REG:
	  return CV_AMD64_XMM5L;

	case XMM6_REG:
	  return CV_AMD64_XMM6L;

	case XMM7_REG:
	  return CV_AMD64_XMM7L;

	case XMM8_REG:
	  return CV_AMD64_XMM8L;

	case XMM9_REG:
	  return CV_AMD64_XMM9L;

	case XMM10_REG:
	  return CV_AMD64_XMM10L;

	case XMM11_REG:
	  return CV_AMD64_XMM11L;

	case XMM12_REG:
	  return CV_AMD64_XMM12L;

	case XMM13_REG:
	  return CV_AMD64_XMM13L;

	case XMM14_REG:
	  return CV_AMD64_XMM14L;

	case XMM15_REG:
	  return CV_AMD64_XMM15L;
	}

      break;

    case E_TImode:
      switch (regno)
	{
	case AX_REG:
	  return CV_AMD64_RAX;

	case DX_REG:
	  return CV_AMD64_RDX;

	case CX_REG:
	  return CV_AMD64_RCX;

	case BX_REG:
	  return CV_AMD64_RBX;

	case SI_REG:
	  return CV_AMD64_RSI;

	case DI_REG:
	  return CV_AMD64_RDI;

	case BP_REG:
	  return CV_AMD64_RBP;

	case SP_REG:
	  return CV_AMD64_RSP;

	case R8_REG:
	  return CV_AMD64_R8;

	case R9_REG:
	  return CV_AMD64_R9;

	case R10_REG:
	  return CV_AMD64_R10;

	case R11_REG:
	  return CV_AMD64_R11;

	case R12_REG:
	  return CV_AMD64_R12;

	case R13_REG:
	  return CV_AMD64_R13;

	case R14_REG:
	  return CV_AMD64_R14;

	case R15_REG:
	  return CV_AMD64_R15;

	case XMM0_REG:
	  return CV_AMD64_XMM0;

	case XMM1_REG:
	  return CV_AMD64_XMM1;

	case XMM2_REG:
	  return CV_AMD64_XMM2;

	case XMM3_REG:
	  return CV_AMD64_XMM3;

	case XMM4_REG:
	  return CV_AMD64_XMM4;

	case XMM5_REG:
	  return CV_AMD64_XMM5;

	case XMM6_REG:
	  return CV_AMD64_XMM6;

	case XMM7_REG:
	  return CV_AMD64_XMM7;

	case XMM8_REG:
	  return CV_AMD64_XMM8;

	case XMM9_REG:
	  return CV_AMD64_XMM9;

	case XMM10_REG:
	  return CV_AMD64_XMM10;

	case XMM11_REG:
	  return CV_AMD64_XMM11;

	case XMM12_REG:
	  return CV_AMD64_XMM12;

	case XMM13_REG:
	  return CV_AMD64_XMM13;

	case XMM14_REG:
	  return CV_AMD64_XMM14;

	case XMM15_REG:
	  return CV_AMD64_XMM15;
	}

      break;

    case E_HImode:
      switch (regno)
	{
	case AX_REG:
	  return CV_AMD64_AX;

	case DX_REG:
	  return CV_AMD64_DX;

	case CX_REG:
	  return CV_AMD64_CX;

	case BX_REG:
	  return CV_AMD64_BX;

	case SI_REG:
	  return CV_AMD64_SI;

	case DI_REG:
	  return CV_AMD64_DI;

	case BP_REG:
	  return CV_AMD64_BP;

	case SP_REG:
	  return CV_AMD64_SP;

	case FLAGS_REG:
	  return CV_AMD64_FLAGS;

	case R8_REG:
	  return CV_AMD64_R8W;

	case R9_REG:
	  return CV_AMD64_R9W;

	case R10_REG:
	  return CV_AMD64_R10W;

	case R11_REG:
	  return CV_AMD64_R11W;

	case R12_REG:
	  return CV_AMD64_R12W;

	case R13_REG:
	  return CV_AMD64_R13W;

	case R14_REG:
	  return CV_AMD64_R14W;

	case R15_REG:
	  return CV_AMD64_R15W;
	}

      break;

    case E_QImode:
      switch (regno)
	{
	case AX_REG:
	  return CV_AMD64_AL;

	case DX_REG:
	  return CV_AMD64_DL;

	case CX_REG:
	  return CV_AMD64_CL;

	case BX_REG:
	  return CV_AMD64_BL;

	case SI_REG:
	  return CV_AMD64_SIL;

	case DI_REG:
	  return CV_AMD64_DIL;

	case BP_REG:
	  return CV_AMD64_BPL;

	case SP_REG:
	  return CV_AMD64_SPL;

	case R8_REG:
	  return CV_AMD64_R8B;

	case R9_REG:
	  return CV_AMD64_R9B;

	case R10_REG:
	  return CV_AMD64_R10B;

	case R11_REG:
	  return CV_AMD64_R11B;

	case R12_REG:
	  return CV_AMD64_R12B;

	case R13_REG:
	  return CV_AMD64_R13B;

	case R14_REG:
	  return CV_AMD64_R14B;

	case R15_REG:
	  return CV_AMD64_R15B;
	}

      break;

    case E_TFmode:
      switch (regno)
	{
	case XMM0_REG:
	  return CV_AMD64_XMM0;

	case XMM1_REG:
	  return CV_AMD64_XMM1;

	case XMM2_REG:
	  return CV_AMD64_XMM2;

	case XMM3_REG:
	  return CV_AMD64_XMM3;

	case XMM4_REG:
	  return CV_AMD64_XMM4;

	case XMM5_REG:
	  return CV_AMD64_XMM5;

	case XMM6_REG:
	  return CV_AMD64_XMM6;

	case XMM7_REG:
	  return CV_AMD64_XMM7;

	case XMM8_REG:
	  return CV_AMD64_XMM8;

	case XMM9_REG:
	  return CV_AMD64_XMM9;

	case XMM10_REG:
	  return CV_AMD64_XMM10;

	case XMM11_REG:
	  return CV_AMD64_XMM11;

	case XMM12_REG:
	  return CV_AMD64_XMM12;

	case XMM13_REG:
	  return CV_AMD64_XMM13;

	case XMM14_REG:
	  return CV_AMD64_XMM14;

	case XMM15_REG:
	  return CV_AMD64_XMM15;
	}

      break;

    case E_XFmode:
      switch (regno)
	{
	case ST0_REG:
	  return CV_AMD64_ST0;

	case ST1_REG:
	  return CV_AMD64_ST1;

	case ST2_REG:
	  return CV_AMD64_ST2;

	case ST3_REG:
	  return CV_AMD64_ST3;

	case ST4_REG:
	  return CV_AMD64_ST4;

	case ST5_REG:
	  return CV_AMD64_ST5;

	case ST6_REG:
	  return CV_AMD64_ST6;

	case ST7_REG:
	  return CV_AMD64_ST7;

	case AX_REG:
	  return CV_AMD64_RAX;

	case DX_REG:
	  return CV_AMD64_RDX;

	case CX_REG:
	  return CV_AMD64_RCX;

	case BX_REG:
	  return CV_AMD64_RBX;

	case SI_REG:
	  return CV_AMD64_RSI;

	case DI_REG:
	  return CV_AMD64_RDI;

	case BP_REG:
	  return CV_AMD64_RBP;

	case SP_REG:
	  return CV_AMD64_RSP;

	case R8_REG:
	  return CV_AMD64_R8;

	case R9_REG:
	  return CV_AMD64_R9;

	case R10_REG:
	  return CV_AMD64_R10;

	case R11_REG:
	  return CV_AMD64_R11;

	case R12_REG:
	  return CV_AMD64_R12;

	case R13_REG:
	  return CV_AMD64_R13;

	case R14_REG:
	  return CV_AMD64_R14;

	case R15_REG:
	  return CV_AMD64_R15;
	}

      break;

    default:
      break;
    }

  warning (0, "could not map amd64 register %u, mode %u to CodeView constant",
	   regno, mode);

  return CV_AMD64_NONE;
}

/* Map a gcc register constant to its CodeView equivalent. */
static unsigned int
map_register_no (unsigned int regno, machine_mode mode)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    return 0;

  if (TARGET_64BIT)
    return (unsigned int) map_register_no_amd64 (regno, mode);
  else
    return (unsigned int) map_register_no_x86 (regno, mode);
}

/* We can't rely on eliminate_regs for stack offsets - it seems that some
 * compiler passes alter the stack without changing the values in the
 * reg_eliminate array that eliminate_regs relies on. */
static int32_t
fix_variable_offset (rtx orig_rtl, unsigned int reg, int32_t offset)
{
  if (!TARGET_64BIT)
    {
      if (reg == CV_X86_EBP)
	{
	  if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
	      REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == ARGP_REG)
	    {
	      return cfun->machine->frame.hard_frame_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == ARGP_REG)
	    return cfun->machine->frame.hard_frame_pointer_offset;
	  else if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
		   REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.hard_frame_pointer_offset -
		cfun->machine->frame.frame_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.hard_frame_pointer_offset -
		cfun->machine->frame.frame_pointer_offset;
	    }
	}
      else if (reg == CV_X86_ESP)
	{
	  if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
	      REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == ARGP_REG)
	    {
	      return cfun->machine->frame.stack_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == ARGP_REG)
	    return cfun->machine->frame.stack_pointer_offset;
	  else if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
		   REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.stack_pointer_offset -
		cfun->machine->frame.frame_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.stack_pointer_offset -
		cfun->machine->frame.frame_pointer_offset;
	    }
	}
    }
  else
    {
      if (reg == CV_AMD64_RBP)
	{
	  if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
	      REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == ARGP_REG)
	    {
	      return cfun->machine->frame.hard_frame_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == ARGP_REG)
	    return cfun->machine->frame.hard_frame_pointer_offset;
	  else if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
		   REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.hard_frame_pointer_offset -
		cfun->machine->frame.frame_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.hard_frame_pointer_offset -
		cfun->machine->frame.frame_pointer_offset;
	    }
	}
      else if (reg == CV_AMD64_RSP)
	{
	  if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
	      GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
	      REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == ARGP_REG)
	    {
	      return cfun->machine->frame.stack_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == ARGP_REG)
	    return cfun->machine->frame.stack_pointer_offset;
	  else if (GET_CODE (XEXP (orig_rtl, 0)) == PLUS &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 0)) == REG &&
		   GET_CODE (XEXP (XEXP (orig_rtl, 0), 1)) == CONST_INT &&
		   REGNO (XEXP (XEXP (orig_rtl, 0), 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.stack_pointer_offset -
		cfun->machine->frame.frame_pointer_offset +
		XINT (XEXP (XEXP (orig_rtl, 0), 1), 0);
	    }
	  else if (REG_P (XEXP (orig_rtl, 0))
		   && REGNO (XEXP (orig_rtl, 0)) == FRAME_REG)
	    {
	      return cfun->machine->frame.stack_pointer_offset -
		cfun->machine->frame.frame_pointer_offset;
	    }
	}
    }

  return offset;
}

/* We've been given a declaration for a local variable. Allocate a
 * pdb_local_var and add it to the list for this scope block. */
static void
add_local (const char *name, tree t, struct pdb_type *type, rtx orig_rtl,
	   unsigned int block_num)
{
  struct pdb_local_var *plv;
  size_t name_len = strlen (name);
  rtx rtl;

  plv =
    (struct pdb_local_var *) xmalloc (offsetof (struct pdb_local_var, name) +
				      name_len + 1);
  plv->next = NULL;
  plv->type = type;
  plv->symbol = NULL;
  plv->t = t;
  plv->block_num = block_num;
  plv->var_type = pdb_local_var_unknown;
  memcpy (plv->name, name, name_len + 1);

  rtl = eliminate_regs (orig_rtl, VOIDmode, NULL_RTX);

  if (MEM_P (rtl))
    {
      if (GET_CODE (XEXP (rtl, 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (rtl, 0), 0)) == REG
	  && GET_CODE (XEXP (XEXP (rtl, 0), 1)) == CONST_INT)
	{
	  plv->var_type = pdb_local_var_regrel;
	  plv->reg =
	    map_register_no (REGNO (XEXP (XEXP (rtl, 0), 0)),
			     GET_MODE (XEXP (XEXP (rtl, 0), 0)));
	  plv->offset = XINT (XEXP (XEXP (rtl, 0), 1), 0);
	}
      else if (REG_P (XEXP (rtl, 0)))
	{
	  plv->var_type = pdb_local_var_regrel;
	  plv->reg =
	    map_register_no (REGNO (XEXP (rtl, 0)), GET_MODE (XEXP (rtl, 0)));
	  plv->offset = 0;
	}
      else if (SYMBOL_REF_P (XEXP (rtl, 0)))
	{
	  plv->var_type = pdb_local_var_symbol;
	  plv->symbol = xstrdup (XSTR (XEXP (rtl, 0), 0));
	}
    }
  else if (REG_P (rtl))
    {
      plv->var_type = pdb_local_var_register;
      plv->reg = map_register_no (REGNO (rtl), GET_MODE (rtl));
    }

  if (plv->var_type == pdb_local_var_regrel)
    plv->offset = fix_variable_offset (orig_rtl, plv->reg, plv->offset);

  if (cur_func->last_local_var)
    cur_func->last_local_var->next = plv;

  cur_func->last_local_var = plv;

  if (!cur_func->local_vars)
    cur_func->local_vars = plv;
}

/* We've encountered a scope block within a function - loop through and
 * add any function declarations, then call recursively for any
 * sub-blocks. */
static void
pdbout_function_decl_block (tree block)
{
  tree f;

  f = BLOCK_VARS (block);
  while (f)
    {
      if (TREE_CODE (f) == VAR_DECL && DECL_RTL_SET_P (f) && DECL_NAME (f))
	{
	  struct pdb_type *type = find_type (TREE_TYPE (f));

	  add_local (IDENTIFIER_POINTER (DECL_NAME (f)), f,
		     type, DECL_RTL (f), BLOCK_NUMBER (block));
	}

      f = TREE_CHAIN (f);
    }

  f = BLOCK_SUBBLOCKS (block);
  while (f)
    {
      pdbout_function_decl_block (f);

      f = BLOCK_CHAIN (f);
    }
}

/* We've encountered a function declaration. Add the parameters as local
 * variables, then loop through and add its scope blocks. */
static void
pdbout_function_decl (tree decl)
{
  tree f;

  if (!cur_func)
    return;

  f = DECL_ARGUMENTS (decl);
  while (f)
    {
      if (TREE_CODE (f) == PARM_DECL && DECL_NAME (f))
	{
	  struct pdb_type *type = find_type (TREE_TYPE (f));

	  add_local (IDENTIFIER_POINTER (DECL_NAME (f)), f,
		     type, DECL_RTL (f), 0);
	}

      f = TREE_CHAIN (f);
    }

  pdbout_function_decl_block (DECL_INITIAL (decl));

  cur_func = NULL;
  cur_block = NULL;
}

/* We've been given the details of where an optimized local variable resides,
 * i.e. one that doesn't stay in the same place on the stack for the function
 * duration. Record them so we can output them later.
 * CodeView seems quite limited in this regard compared to DWARF - e.g. there's
 * no way of saying that we know a variable would always have a constant value
 * at such-and-such a point. There's hints in the header files that such
 * functionality once existed, but MSVC won't output it and the debugger
 * doesn't seem to understand it. */
static void
pdbout_var_location (rtx_insn * loc_note)
{
  rtx value, orig_rtl;
  tree var;
  struct pdb_var_location *var_loc;

  if (!cur_func)
    return;

  if (!NOTE_P (loc_note))
    return;

  if (NOTE_KIND (loc_note) != NOTE_INSN_VAR_LOCATION)
    return;

  var = NOTE_VAR_LOCATION_DECL (loc_note);
  value = orig_rtl = NOTE_VAR_LOCATION_LOC (loc_note);

  if (value)
    value = eliminate_regs (value, VOIDmode, NULL_RTX);

  var_loc =
    (struct pdb_var_location *) xmalloc (sizeof (struct pdb_var_location));

  var_loc->next = NULL;
  var_loc->var = var;
  var_loc->var_loc_number = var_loc_number;

  if (value)
    {
      switch (GET_CODE (value))
	{
	case REG:
	  var_loc->type = pdb_var_loc_register;
	  var_loc->reg = map_register_no (REGNO (value), GET_MODE (value));
	  break;

	case MEM:
	  if (GET_CODE (XEXP (value, 0)) == PLUS
	      && GET_CODE (XEXP (XEXP (value, 0), 0)) == REG
	      && GET_CODE (XEXP (XEXP (value, 0), 1)) == CONST_INT)
	    {
	      var_loc->type = pdb_var_loc_regrel;
	      var_loc->reg =
		map_register_no (REGNO (XEXP (XEXP (value, 0), 0)),
				 GET_MODE (XEXP (XEXP (value, 0), 0)));
	      var_loc->offset = XINT (XEXP (XEXP (value, 0), 1), 0);
	    }
	  else if (GET_CODE (XEXP (value, 0)) == REG)
	    {
	      var_loc->type = pdb_var_loc_regrel;
	      var_loc->reg =
		map_register_no (REGNO (XEXP (value, 0)),
				 GET_MODE (XEXP (value, 0)));
	      var_loc->offset = 0;
	    }
	  else
	    var_loc->type = pdb_var_loc_unknown;

	  break;

	default:
	  var_loc->type = pdb_var_loc_unknown;
	  break;
	}

      if (var_loc->type == pdb_var_loc_regrel)
	var_loc->offset =
	  fix_variable_offset (orig_rtl, var_loc->reg, var_loc->offset);
    }

  fprintf (asm_out_file, ".Lvarloc%u:\n", var_loc_number);

  if (cur_func->last_var_loc)
    cur_func->last_var_loc->next = var_loc;

  cur_func->last_var_loc = var_loc;

  if (!cur_func->var_locs)
    cur_func->var_locs = var_loc;

  var_loc_number++;
}

/* We've encountered the start of a scope block - output an asm label so
 * it can be referred to elsewhere. */
static void
pdbout_begin_block (unsigned int line ATTRIBUTE_UNUSED, unsigned int blocknum)
{
  struct pdb_block *b;

  fprintf (asm_out_file, ".Lblockstart%u:\n", blocknum);

  b = (struct pdb_block *) xmalloc (sizeof (pdb_block));

  if (cur_block->last_child)
    cur_block->last_child->next = b;

  cur_block->last_child = b;

  if (!cur_block->children)
    cur_block->children = b;

  b->parent = cur_block;
  b->num = blocknum;
  b->children = b->last_child = NULL;
  b->next = NULL;

  cur_block = b;
}

/* We've encountered the end of a scope block - output an asm label so
 * it can be referred to elsewhere. */
static void
pdbout_end_block (unsigned int line ATTRIBUTE_UNUSED, unsigned int blocknum)
{
  fprintf (asm_out_file, ".Lblockend%u:\n", blocknum);

  cur_block = cur_block->parent;
}
