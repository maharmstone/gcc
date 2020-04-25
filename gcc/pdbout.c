#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "debug.h"
#include "pdbout.h"
#include "function.h"
#include "output.h"
#include "target.h"
#include "defaults.h"
#include "print-tree.h"

#define FUNC_BEGIN_LABEL	"LFB"
#define FUNC_END_LABEL		"LFE"

static void pdbout_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
				   unsigned int column ATTRIBUTE_UNUSED,
				   const char *file ATTRIBUTE_UNUSED);
static void pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
				 const char *file ATTRIBUTE_UNUSED);
static void pdbout_finish (const char *filename);
static void pdbout_begin_function(tree func);
static void pdbout_late_global_decl(tree var);

static struct pdb_func *funcs = NULL;
static struct pdb_global_var *global_vars = NULL;

const struct gcc_debug_hooks pdb_debug_hooks =
{
  debug_nothing_charstar,		 /* init */
  pdbout_finish,			 /* finish */
  debug_nothing_charstar,		 /* early_finish */
  debug_nothing_void,			 /* assembly_start */
  debug_nothing_int_charstar,		 /* define */
  debug_nothing_int_charstar,		 /* undef */
  debug_nothing_int_charstar,		 /* start_source_file */
  debug_nothing_int,			 /* end_source_file */
  debug_nothing_int_int,	         /* begin_block */
  debug_nothing_int_int,	         /* end_block */
  debug_true_const_tree,	         /* ignore_block */
  debug_nothing_int_int_charstar_int_bool, /* source_line */
  pdbout_begin_prologue,
  debug_nothing_int_charstar,	         /* end_prologue */
  debug_nothing_int_charstar,	         /* begin_epilogue */
  pdbout_end_epilogue,		         /* end_epilogue */
  pdbout_begin_function,	         /* begin_function */
  debug_nothing_int,		         /* end_function */
  debug_nothing_tree,		         /* register_main_translation_unit */
  debug_nothing_tree,		         /* function_decl */
  debug_nothing_tree,		         /* early_global_decl */
  pdbout_late_global_decl,
  debug_nothing_tree_int,		 /* type_decl */
  debug_nothing_tree_tree_tree_bool_bool,/* imported_module_or_decl */
  debug_false_tree_charstarstar_uhwistar,/* die_ref_for_decl */
  debug_nothing_tree_charstar_uhwi,      /* register_external_die */
  debug_nothing_tree,		         /* deferred_inline_function */
  debug_nothing_tree,		         /* outlining_inline_function */
  debug_nothing_rtx_code_label,	         /* label */
  debug_nothing_int,		         /* handle_pch */
  debug_nothing_rtx_insn,		 /* var_location */
  debug_nothing_tree,	         	 /* inline_entry */
  debug_nothing_tree,			 /* size_function */
  debug_nothing_void,                    /* switch_text_section */
  debug_nothing_tree_tree,		 /* set_name */
  0,                                     /* start_end_main_source_file */
  TYPE_SYMTAB_IS_ADDRESS                 /* tree_type_symtab_field */
};

static void
pdbout_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
		       unsigned int column ATTRIBUTE_UNUSED,
		       const char *file ATTRIBUTE_UNUSED)
{
  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, FUNC_BEGIN_LABEL,
			  current_function_funcdef_no);
}

static void
pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
		     const char *file ATTRIBUTE_UNUSED)
{
  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, FUNC_END_LABEL,
			  current_function_funcdef_no);
}

static void
pdbout_lproc32 (struct pdb_func *func)
{
  // start procedure

  // FIXME - don't use labels to do alignment

  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "cvprocstart", func->num);

  fprintf (asm_out_file, "\t.short\t[cvprocstarta%u]-[cvprocstart%u]-2\n", func->num, func->num); // reclen
  fprintf (asm_out_file, "\t.short\t0x%x\n", func->public_flag ? CODEVIEW_S_GPROC32 : CODEVIEW_S_LPROC32);
  fprintf (asm_out_file, "\t.long\t0\n"); // pParent
  fprintf (asm_out_file, "\t.long\t[cvprocend%u]-[.pdb]\n", func->num); // pEnd
  fprintf (asm_out_file, "\t.long\t0\n"); // pNext
  fprintf (asm_out_file, "\t.long\t[" FUNC_END_LABEL "%u]-[" FUNC_BEGIN_LABEL "%u]\n", func->num, func->num); // len
  fprintf (asm_out_file, "\t.long\t0\n"); // FIXME - DbgStart
  fprintf (asm_out_file, "\t.long\t0\n"); // FIXME - DbgEnd
  fprintf (asm_out_file, "\t.long\t0\n"); // FIXME - typeind
  fprintf (asm_out_file, "\t.long\t[" FUNC_BEGIN_LABEL "%u]\n", func->num); // off
  fprintf (asm_out_file, "\t.short\t0\n"); // seg (will get set by the linker)
  fprintf (asm_out_file, "\t.byte\t0\n"); // FIXME - flags
  ASM_OUTPUT_ASCII (asm_out_file, func->name, strlen (func->name) + 1);

  fprintf (asm_out_file, "\t.balign\t4\n");

  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "cvprocstarta", func->num);

  // FIXME - S_FRAMEPROC, S_BPREL32, S_CALLSITEINFO, etc.

  // end procedure

  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "cvprocend", func->num);

  fprintf (asm_out_file, "\t.short\t0x2\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_S_END);
}

static void
pdbout_ldata32 (struct pdb_global_var *v)
{
  size_t name_len = strlen(v->name);
  uint16_t len;

  // Outputs DATASYM32 struct

  len = 15 + name_len;

  if (len % 4 != 0)
    len += 4 - (len % 4);

  fprintf (asm_out_file, "\t.short\t0x%x\n", (uint16_t)(len - sizeof(uint16_t))); // reclen
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_S_LDATA32); // FIXME - S_GDATA32 if not static
  fprintf (asm_out_file, "\t.long\t0\n"); // FIXME - type

  fprintf (asm_out_file, "\t.long\t["); // off
  ASM_OUTPUT_LABELREF (asm_out_file, v->asm_name);
  fprintf (asm_out_file, "]\n");

  fprintf (asm_out_file, "\t.short\t0\n"); // seg (will get set by the linker)
  ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);

  fprintf (asm_out_file, "\t.balign\t4\n");
}

static void
pdbout_finish (const char *filename ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, "\t.section\t.pdb, \"ndr\"\n");
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_SIGNATURE_C13);

  while (global_vars) {
    struct pdb_global_var *n;

    pdbout_ldata32(global_vars);

    n = global_vars->next;

    if (global_vars->name)
      free (global_vars->name);

    if (global_vars->asm_name)
      free (global_vars->asm_name);

    free(global_vars);

    global_vars = n;
  }

  while (funcs) {
    struct pdb_func *n;

    pdbout_lproc32(funcs);

    n = funcs->next;

    if (funcs->name)
      free (funcs->name);

    free(funcs);

    funcs = n;
  }
}

static void
pdbout_begin_function (tree func)
{
  struct pdb_func *f = (struct pdb_func*)xmalloc(sizeof(struct pdb_func));

  f->next = funcs;
  f->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(func)));
  f->num = current_function_funcdef_no;
  f->public_flag = func->base.public_flag;

  funcs = f;
}

static void pdbout_late_global_decl(tree var)
{
  if (TREE_CODE (var) != VAR_DECL)
    return;

  struct pdb_global_var *v = (struct pdb_global_var*)xmalloc(sizeof(struct pdb_global_var));

  v->next = global_vars;
  v->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(var)));
  v->asm_name = xstrdup((const char*)var->var_decl.common.assembler_name->identifier.id.str); // FIXME - is this guaranteed to be null-terminated?

  // FIXME - record type
  // FIXME - record whether static or not

  global_vars = v;
}
