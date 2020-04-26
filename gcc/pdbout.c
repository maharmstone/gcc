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
#include "print-tree.h" // FIXME - remove this

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
static void pdbout_type_decl(tree t, int local ATTRIBUTE_UNUSED);

static uint16_t find_type(tree t);

static struct pdb_func *funcs = NULL;
static struct pdb_global_var *global_vars = NULL;
static struct pdb_type *types = NULL, *last_type = NULL;
static uint16_t type_num = 0x1000;

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
  pdbout_type_decl,
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
  fprintf (asm_out_file, "\t.short\t0x%x\n", v->public_flag ? CODEVIEW_S_GDATA32 : CODEVIEW_S_LDATA32);
  fprintf (asm_out_file, "\t.long\t0\n"); // FIXME - type

  fprintf (asm_out_file, "\t.long\t["); // off
  ASM_OUTPUT_LABELREF (asm_out_file, v->asm_name);
  fprintf (asm_out_file, "]\n");

  fprintf (asm_out_file, "\t.short\t0\n"); // seg (will get set by the linker)
  ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);

  fprintf (asm_out_file, "\t.balign\t4\n");
}

static void
write_pdb_section()
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
free_type(struct pdb_type *t)
{
  switch (t->cv_type) {
    case CODEVIEW_LF_FIELDLIST:
    {
      struct pdb_fieldlist* fl = (struct pdb_fieldlist*)t->data;

      for (unsigned int i = 0; i < fl->count; i++) {
	if (fl->entries[i].name)
	  free(fl->entries[i].name);
      }

      free(fl->entries);

      break;
    }
  }

  free(t);
}

static void
write_fieldlist(struct pdb_fieldlist *fl)
{
  unsigned int len = 4;

  for (unsigned int i = 0; i < fl->count; i++) {
    len += 2;

    if (fl->entries[i].cv_type == CODEVIEW_LF_MEMBER)
      len += 9 + strlen(fl->entries[i].name);

    if (len % 4 != 0)
      len += 4 - (len % 4);
  }

  fprintf (asm_out_file, "\t.short\t0x%x\n", len - 2);
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_FIELDLIST);

  for (unsigned int i = 0; i < fl->count; i++) {
    fprintf (asm_out_file, "\t.short\t0x%x\n", fl->entries[i].cv_type);

    if (fl->entries[i].cv_type == CODEVIEW_LF_MEMBER) {
      size_t name_len = strlen(fl->entries[i].name);
      unsigned int align;

      fprintf (asm_out_file, "\t.short\t0x%x\n", fl->entries[i].fld_attr);
      fprintf (asm_out_file, "\t.short\t0x%x\n", fl->entries[i].type);
      fprintf (asm_out_file, "\t.short\t0\n"); // padding
      fprintf (asm_out_file, "\t.short\t0x%x\n", fl->entries[i].offset);
      ASM_OUTPUT_ASCII (asm_out_file, fl->entries[i].name, name_len + 1);

      // handle alignment padding

      align = 4 - ((3 + name_len) % 4);

      if (align != 4) {
	if (align == 3)
	  fprintf (asm_out_file, "\t.byte\t0xf3\n");

	if (align >= 2)
	  fprintf (asm_out_file, "\t.byte\t0xf2\n");

	fprintf (asm_out_file, "\t.byte\t0xf1\n");
      }

      fprintf (asm_out_file, "\t.balign\t4\n");
    }
  }

  fprintf (asm_out_file, "\t.balign\t4\n");
}

static void
write_type(struct pdb_type *t)
{
  switch (t->cv_type) {
    case CODEVIEW_LF_FIELDLIST:
      write_fieldlist((struct pdb_fieldlist*)t->data);
    break;
  }
}

static void
write_pdb_type_section()
{
  fprintf (asm_out_file, "\t.section\t.pdb$typ, \"ndr\"\n");

  while (types) {
    struct pdb_type *n;

    write_type(types);

    n = types->next;

    free_type(types);

    types = n;
  }
}

static void
pdbout_finish (const char *filename ATTRIBUTE_UNUSED)
{
  write_pdb_section();
  write_pdb_type_section();
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
  v->public_flag = var->base.public_flag;

  // FIXME - record type

  global_vars = v;
}

static uint16_t
find_type_struct(tree t)
{
  tree f;
  struct pdb_type *fltype;
  struct pdb_fieldlist *fieldlist;
  struct pdb_fieldlist_entry *ent;
  unsigned int num_entries = 0;

  // FIXME - what about self-referencing structs?

  printf("STRUCT %s\n", IDENTIFIER_POINTER(TYPE_NAME(t)));
  // FIXME - size

  f = t->type_non_common.values;

  while (f) {
    num_entries++;
    f = f->common.chain;
  }

  // FIXME - check fieldlist doesn't already exist

  // add fieldlist type

  fltype = (struct pdb_type *)xmalloc(offsetof(pdb_type, data) + sizeof(struct pdb_fieldlist));

  fltype->next = NULL;

  fltype->id = type_num;
  type_num++;

  fltype->tree = NULL;
  fltype->cv_type = CODEVIEW_LF_FIELDLIST;

  if (last_type)
    last_type->next = fltype;

  if (!types)
    types = fltype;

  last_type = fltype;

  fieldlist = (struct pdb_fieldlist*)fltype->data;
  fieldlist->count = num_entries;
  fieldlist->entries = (struct pdb_fieldlist_entry*)xmalloc(sizeof(struct pdb_fieldlist_entry) * num_entries);

  ent = fieldlist->entries;
  f = t->type_non_common.values;

  while (f) {
    unsigned int bit_offset = (TREE_INT_CST_ELT(DECL_FIELD_OFFSET(f), 0) * 8) + TREE_INT_CST_ELT(DECL_FIELD_BIT_OFFSET(f), 0);

    ent->cv_type = CODEVIEW_LF_MEMBER;
    ent->type = find_type(f->common.typed.type);
    ent->offset = bit_offset / 8; // FIXME - what about bit fields?
    ent->fld_attr = CV_FLDATTR_PUBLIC; // FIXME?
    ent->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(f)));

    f = f->common.chain;
    ent++;
  }

  // FIXME - add type for struct

  return 0;
}

static uint16_t
find_type(tree t)
{
  // FIXME - identify builtins

  // FIXME - look for pointer in existing list

  // FIXME - only add new if told to

  // FIXME - integer_type
  // FIXME - real_type
  // FIXME - void_type
  // FIXME - unions
  // FIXME - enums
  // FIXME - pointers
  // FIXME - constness etc.
  // FIXME - any others?

  if (t->base.code == RECORD_TYPE)
    return find_type_struct(t);
  else
    return 0;
}

static void pdbout_type_decl(tree t, int local ATTRIBUTE_UNUSED)
{
  if (DECL_IN_SYSTEM_HEADER(t)) // ignoring system headers for now (FIXME)
    return;

  // FIXME - if from file in /usr/include or wherever, only include in output if used

  find_type(t->typed.type);
}
