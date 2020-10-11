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
#include "config/i386/i386-protos.h"
#include "md5.h"
#include "rtl.h"
#include "insn-config.h"
#include "reload.h"
#include "cp/cp-tree.h"
#include "common/common-target.h"
#include "except.h"

#define FUNC_BEGIN_LABEL	".startfunc"
#define FUNC_END_LABEL		".endfunc"

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
				const char *text ATTRIBUTE_UNUSED,
				int discriminator ATTRIBUTE_UNUSED,
				bool is_stmt ATTRIBUTE_UNUSED);
static void pdbout_function_decl (tree decl);
static void pdbout_var_location (rtx_insn * loc_note);
static void pdbout_begin_block (unsigned int line ATTRIBUTE_UNUSED,
				unsigned int blocknum);
static void pdbout_end_block (unsigned int line ATTRIBUTE_UNUSED,
			      unsigned int blocknum);

static uint16_t find_type (tree t);

static struct pdb_func *funcs = NULL, *cur_func = NULL;
static struct pdb_block *cur_block = NULL;
static struct pdb_global_var *global_vars = NULL;
static struct pdb_type *types = NULL, *last_type = NULL;
static uint16_t type_num = FIRST_TYPE_NUM;
static struct pdb_source_file *source_files = NULL, *last_source_file = NULL;
static uint32_t source_file_string_offset = 1;
static unsigned int num_line_number_entries = 0;
static unsigned int num_source_files = 0;
static unsigned int var_loc_number = 1;

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
      fprintf (asm_out_file, "\t.long\t[.varloc%u]\n",
	       var_loc->var_loc_number);

      // section (will be filled in by the linker)
      fprintf (asm_out_file, "\t.short\t0\n");

      if (next_var_loc_number != 0)
	fprintf (asm_out_file, "\t.short\t[.varloc%u]-[.varloc%u]\n",
		 next_var_loc_number, var_loc->var_loc_number);
      else {
	fprintf (asm_out_file,
		 "\t.short\t[" FUNC_END_LABEL "%u]-[.varloc%u]\n",
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
      fprintf (asm_out_file, "\t.long\t[.varloc%u]\n",
	       var_loc->var_loc_number);

      // section (will be filled in by the linker)
      fprintf (asm_out_file, "\t.short\t0\n");

      if (next_var_loc_number != 0)
	fprintf (asm_out_file, "\t.short\t[.varloc%u]-[.varloc%u]\n",
		 next_var_loc_number, var_loc->var_loc_number);
      else {
	fprintf (asm_out_file,
		 "\t.short\t[" FUNC_END_LABEL "%u]-[.varloc%u]\n",
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
  fprintf (asm_out_file, "\t.long\t0x%x\n", v->type);
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
	  fprintf (asm_out_file, "\t.long\t0x%x\n", v->type);

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
	  fprintf (asm_out_file, "\t.long\t0x%x\n", v->type);
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
      fprintf (asm_out_file, "\t.long\t0x%x\n", v->type);
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
      fprintf (asm_out_file, "\t.short\t0x%x\n", v->type);
      fprintf (asm_out_file, "\t.short\t0\n");

      fprintf (asm_out_file, "\t.long\t[");	// off
      ASM_OUTPUT_LABELREF (asm_out_file, v->symbol);
      fprintf (asm_out_file, "]\n");

      // section (will get set by the linker)
      fprintf (asm_out_file, "\t.short\t0\n");
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

      fprintf (asm_out_file, ".cvblockstart%u:\n", block->children->num);
      fprintf (asm_out_file, "\t.short\t0x16\n");	// reclen
      fprintf (asm_out_file, "\t.short\t0x%x\n", S_BLOCK32);

      // pParent
      if (block->num != 0) {
	fprintf (asm_out_file, "\t.long\t[.cvblockstart%u]-[.debug$S]\n",
		 block->num);
      } else {
	fprintf (asm_out_file, "\t.long\t[.cvprocstart%u]-[.debug$S]\n",
		 func->num);
      }

      fprintf (asm_out_file, "\t.long\t[.cvblockend%u]-[.debug$S]\n",
	       block->children->num);	// pEnd
      fprintf (asm_out_file, "\t.long\t[.blockend%u]-[.blockstart%u]\n",
	       block->children->num, block->children->num);	// length
      fprintf (asm_out_file, "\t.long\t[.blockstart%u]\n",
	       block->children->num);	// offset

      // section (will be filled in by the linker)
      fprintf (asm_out_file, "\t.short\t0\n");
      fprintf (asm_out_file, "\t.byte\t0\n");	// name (zero-length string)
      fprintf (asm_out_file, "\t.byte\t0\n");	// padding

      pdbout_block (block->children, func);

      fprintf (asm_out_file, ".cvblockend%u:\n", block->children->num);
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

  pdbout_block (&func->block, func);

  // end procedure

  fprintf (asm_out_file, ".cvprocend%u:\n", func->num);

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

/* Output names of the files which make up this translation unit,
 * along with their MD5 checksums. */
static void
write_file_checksums ()
{
  fprintf (asm_out_file, "\t.long\t0x%x\n", DEBUG_S_FILECHKSMS);
  fprintf (asm_out_file, "\t.long\t[.chksumsend]-[.chksumsstart]\n");
  fprintf (asm_out_file, ".chksumsstart:\n");

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

  fprintf (asm_out_file, ".chksumsend:\n");
}

/* Loop through each function, and output the line number to address mapping. */
static void
write_line_numbers ()
{
  struct pdb_func *func = funcs;

  while (func)
    {
      struct pdb_line *l;
      unsigned int num_entries = 0;

      if (!func->lines)
	{
	  func = func->next;
	  continue;
	}

      l = func->lines;
      while (l)
	{
	  num_entries++;
	  l = l->next;
	}

      fprintf (asm_out_file, "\t.long\t0x%x\n", DEBUG_S_LINES);
      fprintf (asm_out_file, "\t.long\t[.linesend%u]-[.linesstart%u]\n",
	       func->num, func->num);
      fprintf (asm_out_file, ".linesstart%u:\n", func->num);

      fprintf (asm_out_file, "\t.long\t[" FUNC_BEGIN_LABEL "%u]\n",
	       func->num);	// address

      // section (filled in by linker)
      fprintf (asm_out_file, "\t.short\t0\n");

      fprintf (asm_out_file, "\t.short\t0\n");	// flags
      fprintf (asm_out_file,
	       "\t.long\t[" FUNC_END_LABEL "%u]-[" FUNC_BEGIN_LABEL "%u]\n",
	       func->num, func->num);	// length

      // file ID (0x18 is size of checksum struct)
      fprintf (asm_out_file, "\t.long\t0x%x\n", func->source_file * 0x18);
      fprintf (asm_out_file, "\t.long\t0x%x\n", num_entries);
      // length of file block
      fprintf (asm_out_file, "\t.long\t0x%x\n", 0xc + (num_entries * 8));

      l = func->lines;
      while (l)
	{
	  fprintf (asm_out_file,
		   "\t.long\t[.line%u]-[" FUNC_BEGIN_LABEL "%u]\n",
		   l->entry, func->num);	// offset
	  fprintf (asm_out_file, "\t.long\t0x%x\n", l->line);	// line no.

	  l = l->next;
	}

      while (func->lines)
	{
	  struct pdb_line *n = func->lines->next;

	  free (func->lines);

	  func->lines = n;
	}

      fprintf (asm_out_file, ".linesend%u:\n", func->num);

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

  fprintf (asm_out_file, "\t.long\t0x%x\n", DEBUG_S_STRINGTABLE);
  fprintf (asm_out_file, "\t.long\t[.strtableend]-[.strtablestart]\n");
  fprintf (asm_out_file, ".strtablestart:\n");
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
  fprintf (asm_out_file, ".strtableend:\n");

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
      fprintf (asm_out_file, "\t.short\t0x%x\n", arglist->args[i]);
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
  fprintf (asm_out_file, "\t.short\t0x%x\n", proc->return_type);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
  fprintf (asm_out_file, "\t.byte\t0x%x\n", proc->calling_convention);
  fprintf (asm_out_file, "\t.byte\t0x%x\n", proc->attributes);
  fprintf (asm_out_file, "\t.short\t0x%x\n", proc->num_args);
  fprintf (asm_out_file, "\t.short\t0x%x\n", proc->arg_list);
  fprintf (asm_out_file, "\t.short\t0\n");	// padding
}

/* Given a pdb_type, output its definition. */
static void
write_type (struct pdb_type *t)
{
  switch (t->cv_type)
    {
    case LF_ARGLIST:
      write_arglist ((struct pdb_arglist *) t->data);
      break;

    case LF_PROCEDURE:
      write_procedure ((struct pdb_proc *) t->data);
      break;
    }
}

/* Output the .debug$T section, which contains all the types used. */
static void
write_pdb_type_section (void)
{
  fprintf (asm_out_file, "\t.section\t.debug$T, \"ndr\"\n");
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_SIGNATURE_C13);

  while (types)
    {
      struct pdb_type *n;

      write_type (types);

      n = types->next;

      free_type (types);

      types = n;
    }
}

/* We've finished compilation - output the .debug$S and .debug$T sections
 * to the ASM file. */
static void
pdbout_finish (const char *filename ATTRIBUTE_UNUSED)
{
  write_pdb_section ();
  write_pdb_type_section ();
}

/* We've been passed a function definition - allocate and initialize a pdb_func
 * struct to represent it. */
static void
pdbout_begin_function (tree func)
{
  expanded_location xloc;
  struct pdb_source_file *psf;
  struct pdb_func *f = (struct pdb_func *) xmalloc (sizeof (struct pdb_func));

  f->next = funcs;
  f->name = xstrdup (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (func)));
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

  f->source_file = 0;

  psf = source_files;
  while (psf)
    {
      if (!strcmp (xloc.file, psf->name))
	{
	  f->source_file = psf->num;
	  break;
	}

      psf = psf->next;
    }

  if (xloc.line != 0)
    pdbout_source_line (xloc.line, 0, NULL, 0, 0);
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

/* Given a new type t, search through the list of existing types. If it's a
 * duplicate of an existing type, free t. Otherwise, add t to the end of
 * the list. */
static uint16_t
add_type (struct pdb_type *t)
{
  struct pdb_type *t2 = types;

  // check for dupes

  while (t2)
    {
      if (t2->cv_type == t->cv_type)
	{
	  switch (t2->cv_type)
	    {
	    case LF_ARGLIST:
	      {
		struct pdb_arglist *arglist1 = (struct pdb_arglist *) t->data;
		struct pdb_arglist *arglist2 =
		  (struct pdb_arglist *) t2->data;

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

			return t2->id;
		      }
		  }

		break;
	      }

	    case LF_PROCEDURE:
	      {
		struct pdb_proc *proc1 = (struct pdb_proc *) t->data;
		struct pdb_proc *proc2 = (struct pdb_proc *) t2->data;

		if (proc1->return_type == proc2->return_type &&
		    proc1->calling_convention == proc2->calling_convention &&
		    proc1->attributes == proc2->attributes &&
		    proc1->num_args == proc2->num_args &&
		    proc1->arg_list == proc2->arg_list)
		  {
		    free (t);

		    return t2->id;
		  }

		break;
	      }
	    }
	}

      t2 = t2->next;
    }

  // add new

  t->next = NULL;

  t->id = type_num;
  type_num++;

  if (last_type)
    last_type->next = t;

  if (!types)
    types = t;

  last_type = t;

  return t->id;
}

/* Given a function type t, allocate a new pdb_type and add it to the
 * type list. */
static uint16_t
find_type_function (tree t)
{
  struct pdb_type *arglisttype, *proctype;
  struct pdb_arglist *arglist;
  struct pdb_proc *proc;
  tree arg;
  unsigned int num_args = 0;
  uint16_t *argptr;
  uint16_t arglisttypenum;

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
				 (num_args * sizeof (uint16_t)));
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

  arglisttypenum = add_type (arglisttype);

  // create procedure

  proctype =
    (struct pdb_type *) xmalloc (offsetof (struct pdb_type, data) +
				 sizeof (struct pdb_proc));
  proctype->cv_type = LF_PROCEDURE;
  proctype->tree = t;

  proc = (struct pdb_proc *) proctype->data;

  proc->return_type = find_type (TREE_TYPE (t));
  proc->attributes = 0;
  proc->num_args = num_args;
  proc->arg_list = arglisttypenum;

  if (TARGET_64BIT)
    proc->calling_convention = CV_CALL_NEAR_C;
  else
    {
      switch (ix86_get_callcvt (t))
	{
	case IX86_CALLCVT_CDECL:
	  proc->calling_convention = CV_CALL_NEAR_C;
	  break;

	case IX86_CALLCVT_STDCALL:
	  proc->calling_convention = CV_CALL_NEAR_STD;
	  break;

	case IX86_CALLCVT_FASTCALL:
	  proc->calling_convention = CV_CALL_NEAR_FAST;
	  break;

	case IX86_CALLCVT_THISCALL:
	  proc->calling_convention = CV_CALL_THISCALL;
	  break;

	default:
	  proc->calling_convention = CV_CALL_NEAR_C;
	}
    }

  return add_type (proctype);
}

/* Resolve a type t to a type number. If it's a builtin type, such as bool or
 * the various ints, return its constant. Otherwise, allocate a new pdb_type,
 * and add it to the type list. */
static uint16_t
find_type (tree t)
{
  struct pdb_type *type;

  if (!t)
    return 0;

  // search through existing types

  type = types;
  while (type)
    {
      if (type->tree == t)
	return type->id;

      type = type->next;
    }

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

  if (TYPE_MAIN_VARIANT (t) != t)
    {
      type = types;
      while (type)
	{
	  if (type->tree == TYPE_MAIN_VARIANT (t))
	      return type->id;

	  type = type->next;
	}
    }

  switch (TREE_CODE (t))
    {
    case FUNCTION_TYPE:
    case METHOD_TYPE:
      return find_type_function (t);

    default:
      return 0;
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

/* We've encountered a new line of source code. Add an ASM label for this,
 * and record the mapping for later. */
static void
pdbout_source_line (unsigned int line, unsigned int column ATTRIBUTE_UNUSED,
		    const char *text ATTRIBUTE_UNUSED,
		    int discriminator ATTRIBUTE_UNUSED,
		    bool is_stmt ATTRIBUTE_UNUSED)
{
  struct pdb_line *ent;

  if (!cur_func)
    return;

  if (cur_func->last_line && cur_func->last_line->line == line)
    return;

  ent = (struct pdb_line *) xmalloc (sizeof (struct pdb_line));

  ent->next = NULL;
  ent->line = line;
  ent->entry = num_line_number_entries;

  if (cur_func->last_line)
    cur_func->last_line->next = ent;

  cur_func->last_line = ent;

  if (!cur_func->lines)
    cur_func->lines = ent;

  fprintf (asm_out_file, ".line%u:\n", num_line_number_entries);

  num_line_number_entries++;
}

/* Given an x86 gcc register no., return the CodeView equivalent. */
static enum pdb_x86_register
map_register_no_x86 (unsigned int regno, machine_mode mode)
{
  if (mode == E_SImode)
    {
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
    }
  else if (mode == E_HImode)
    {
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
    }
  else if (mode == E_QImode)
    {
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
    }
  else if (mode == E_SFmode || mode == E_DFmode)
    {
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
	}
    }

  warning (0, "could not map x86 register %u, mode %u to CodeView constant",
	   regno, mode);

  return CV_X86_NONE;
}

/* Given an amd64 gcc register no., return the CodeView equivalent. */
static enum pdb_amd64_register
map_register_no_amd64 (unsigned int regno, machine_mode mode)
{
  if (mode == E_SImode)
    {
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
	}
    }
  else if (mode == E_DImode)
    {
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
	}
    }
  else if (mode == E_HImode)
    {
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
    }
  else if (mode == E_QImode)
    {
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
    }
  else if (mode == E_SFmode || mode == E_DFmode)
    {
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

/* We've been given a declaration for a local variable. Allocate a
 * pdb_local_var and add it to the list for this scope block. */
static void
add_local (const char *name, tree t, uint16_t type, rtx rtl,
	   unsigned int block_num)
{
  struct pdb_local_var *plv;
  size_t name_len = strlen (name);

  rtl = eliminate_regs (rtl, VOIDmode, NULL_RTX);

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

  /* If using sjlj exceptions on x86, the stack will later get shifted by
   * 16 bytes - we need to account for that now. */
  if (!TARGET_64BIT)
    {
      if (plv->var_type == pdb_local_var_regrel &&
	  plv->reg == CV_X86_EBP &&
	  plv->offset < 0 &&
	  cfun->eh->region_tree &&
	  targetm_common.except_unwind_info (&global_options) == UI_SJLJ)
	{
	  plv->offset -= 16;
	}
    }

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
	  add_local (IDENTIFIER_POINTER (DECL_NAME (f)), f,
		     find_type (TREE_TYPE (f)), DECL_RTL (f),
		     BLOCK_NUMBER (block));
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
	  add_local (IDENTIFIER_POINTER (DECL_NAME (f)), f,
		     find_type (TREE_TYPE (f)),
		     f->parm_decl.common.rtl, 0);
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
  rtx value;
  tree var;
  struct pdb_var_location *var_loc;

  if (!cur_func)
    return;

  if (!NOTE_P (loc_note))
    return;

  if (NOTE_KIND (loc_note) != NOTE_INSN_VAR_LOCATION)
    return;

  var = NOTE_VAR_LOCATION_DECL (loc_note);
  value = NOTE_VAR_LOCATION_LOC (loc_note);

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
    }

  /* If using sjlj exceptions on x86, the stack will later get shifted by
   * 16 bytes - we need to account for that now. */
  if (!TARGET_64BIT)
    {
      if (var_loc->type == pdb_var_loc_regrel &&
	  var_loc->reg == CV_X86_EBP &&
	  var_loc->offset < 0 &&
	  cfun->eh->region_tree &&
	  targetm_common.except_unwind_info (&global_options) == UI_SJLJ)
	{
	  var_loc->offset -= 16;
	}
    }

  fprintf (asm_out_file, ".varloc%u:\n", var_loc_number);

  if (cur_func->last_var_loc)
    cur_func->last_var_loc->next = var_loc;

  cur_func->last_var_loc = var_loc;

  if (!cur_func->var_locs)
    cur_func->var_locs = var_loc;

  var_loc_number++;
}

/* We've encountered the start of a scope block - output an ASM label so
 * it can be referred to elsewhere. */
static void
pdbout_begin_block (unsigned int line ATTRIBUTE_UNUSED, unsigned int blocknum)
{
  struct pdb_block *b;

  fprintf (asm_out_file, ".blockstart%u:\n", blocknum);

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

/* We've encountered the end of a scope block - output an ASM label so
 * it can be referred to elsewhere. */
static void
pdbout_end_block (unsigned int line ATTRIBUTE_UNUSED, unsigned int blocknum)
{
  fprintf (asm_out_file, ".blockend%u:\n", blocknum);

  cur_block = cur_block->parent;
}
