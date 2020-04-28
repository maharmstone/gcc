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
#include "config/i386/i386-protos.h"
#include "print-tree.h" // FIXME - remove this

#define FUNC_BEGIN_LABEL	".LFB"
#define FUNC_END_LABEL		".LFE"

#define FIRST_TYPE_NUM		0x1000

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
static uint16_t type_num = FIRST_TYPE_NUM;

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
  fprintf (asm_out_file, FUNC_BEGIN_LABEL "%u:\n", current_function_funcdef_no);
}

static void
pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
		     const char *file ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, FUNC_END_LABEL "%u:\n", current_function_funcdef_no);
}

static void
pdbout_proc32 (struct pdb_func *func)
{
  size_t name_len = strlen(func->name);
  uint16_t len = 40 + name_len, align;

  // start procedure

  if (len % 4 != 0) {
    align = 4 - (len % 4);
    len += 4 - (len % 4);
  } else
    align = 0;

  fprintf (asm_out_file, "\t.short\t0x%x\n", (uint16_t)(len - sizeof(uint16_t))); // reclen
  fprintf (asm_out_file, "\t.short\t0x%x\n", func->public_flag ? CODEVIEW_S_GPROC32 : CODEVIEW_S_LPROC32);
  fprintf (asm_out_file, "\t.long\t0\n"); // pParent
  fprintf (asm_out_file, "\t.long\t[.cvprocend%u]-[.pdb]\n", func->num); // pEnd
  fprintf (asm_out_file, "\t.long\t0\n"); // pNext
  fprintf (asm_out_file, "\t.long\t[" FUNC_END_LABEL "%u]-[" FUNC_BEGIN_LABEL "%u]\n", func->num, func->num); // len
  fprintf (asm_out_file, "\t.long\t0\n"); // FIXME - DbgStart
  fprintf (asm_out_file, "\t.long\t0\n"); // FIXME - DbgEnd
  fprintf (asm_out_file, "\t.short\t0x%x\n", func->type);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.long\t[" FUNC_BEGIN_LABEL "%u]\n", func->num); // off
  fprintf (asm_out_file, "\t.short\t0\n"); // seg (will get set by the linker)
  fprintf (asm_out_file, "\t.byte\t0\n"); // FIXME - flags
  ASM_OUTPUT_ASCII (asm_out_file, func->name, name_len + 1);

  if (align == 3)
    fprintf (asm_out_file, "\t.byte\t0\n");

  if (align >= 2)
    fprintf (asm_out_file, "\t.byte\t0\n");

  if (align >= 1)
    fprintf (asm_out_file, "\t.byte\t0\n");

  // FIXME - S_FRAMEPROC, S_BPREL32, S_CALLSITEINFO, etc.

  // end procedure

  fprintf (asm_out_file, ".cvprocend%u:\n", func->num);

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
  fprintf (asm_out_file, "\t.short\t0x%x\n", v->type);
  fprintf (asm_out_file, "\t.short\t0\n");

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

    pdbout_proc32(funcs);

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
      struct pdb_fieldlist *fl = (struct pdb_fieldlist*)t->data;

      for (unsigned int i = 0; i < fl->count; i++) {
	if (fl->entries[i].name)
	  free(fl->entries[i].name);
      }

      free(fl->entries);

      break;
    }

    case CODEVIEW_LF_CLASS:
    case CODEVIEW_LF_STRUCTURE:
    case CODEVIEW_LF_UNION:
    {
      struct pdb_struct *str = (struct pdb_struct*)t->data;

      if (str->name)
	free(str->name);

      break;
    }

    case CODEVIEW_LF_ENUM:
    {
      struct pdb_enum *en = (struct pdb_enum*)t->data;

      if (en->name)
	free(en->name);

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
    else if (fl->entries[i].cv_type == CODEVIEW_LF_ENUMERATE)
      len += 5 + strlen(fl->entries[i].name);

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
    } else if (fl->entries[i].cv_type == CODEVIEW_LF_ENUMERATE) {
      size_t name_len = strlen(fl->entries[i].name);
      unsigned int align;

      fprintf (asm_out_file, "\t.short\t0x%x\n", fl->entries[i].fld_attr);
      fprintf (asm_out_file, "\t.short\t0x%x\n", fl->entries[i].value);
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
    }
  }
}

static void
write_struct(uint16_t type, struct pdb_struct *str)
{
  size_t name_len = strlen(str->name);
  unsigned int len = 23 + name_len, align;

  if (len % 4 != 0)
    len += 4 - (len % 4);

  fprintf (asm_out_file, "\t.short\t0x%x\n", len - 2);
  fprintf (asm_out_file, "\t.short\t0x%x\n", type);
  fprintf (asm_out_file, "\t.short\t0x%x\n", str->count);
  fprintf (asm_out_file, "\t.short\t0\n"); // FIXME - property
  fprintf (asm_out_file, "\t.short\t0x%x\n", str->field);
  fprintf (asm_out_file, "\t.short\t0\n"); // derived
  fprintf (asm_out_file, "\t.short\t0\n"); // vshape

  fprintf (asm_out_file, "\t.short\t0\n"); // FIXME
  fprintf (asm_out_file, "\t.short\t0\n"); // FIXME
  fprintf (asm_out_file, "\t.short\t0\n"); // FIXME
  fprintf (asm_out_file, "\t.short\t0x%x\n", str->size);

  ASM_OUTPUT_ASCII (asm_out_file, str->name, name_len + 1);

  // FIXME - unique name?

  align = 4 - ((3 + name_len) % 4);

  if (align != 4) {
    if (align == 3)
      fprintf (asm_out_file, "\t.byte\t0xf3\n");

    if (align >= 2)
      fprintf (asm_out_file, "\t.byte\t0xf2\n");

    fprintf (asm_out_file, "\t.byte\t0xf1\n");
  }
}

static void
write_union(struct pdb_struct *str)
{
  size_t name_len = strlen(str->name);
  unsigned int len = 15 + name_len, align;

  if (len % 4 != 0)
    len += 4 - (len % 4);

  fprintf (asm_out_file, "\t.short\t0x%x\n", len - 2);
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_UNION);
  fprintf (asm_out_file, "\t.short\t0x%x\n", str->count);
  fprintf (asm_out_file, "\t.short\t0\n"); // FIXME - property
  fprintf (asm_out_file, "\t.short\t0x%x\n", str->field);
  fprintf (asm_out_file, "\t.short\t0\n"); // FIXME
  fprintf (asm_out_file, "\t.short\t0x%x\n", str->size);

  ASM_OUTPUT_ASCII (asm_out_file, str->name, name_len + 1);

  // FIXME - unique name?

  align = 4 - ((3 + name_len) % 4);

  if (align != 4) {
    if (align == 3)
      fprintf (asm_out_file, "\t.byte\t0xf3\n");

    if (align >= 2)
      fprintf (asm_out_file, "\t.byte\t0xf2\n");

    fprintf (asm_out_file, "\t.byte\t0xf1\n");
  }
}

static void
write_enum(struct pdb_enum *en)
{
  size_t name_len = strlen(en->name);
  unsigned int len = 17 + name_len, align;

  if (len % 4 != 0)
    len += 4 - (len % 4);

  fprintf (asm_out_file, "\t.short\t0x%x\n", len - 2);
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_ENUM);
  fprintf (asm_out_file, "\t.short\t0x%x\n", en->count);
  fprintf (asm_out_file, "\t.short\t0\n"); // FIXME - property
  fprintf (asm_out_file, "\t.short\t0x%x\n", en->type);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.short\t0x%x\n", en->field);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding

  ASM_OUTPUT_ASCII (asm_out_file, en->name, name_len + 1);

  align = 4 - ((1 + name_len) % 4);

  if (align != 4) {
    if (align == 3)
      fprintf (asm_out_file, "\t.byte\t0xf3\n");

    if (align >= 2)
      fprintf (asm_out_file, "\t.byte\t0xf2\n");

    fprintf (asm_out_file, "\t.byte\t0xf1\n");
  }
}

static void
write_pointer(struct pdb_pointer *ptr)
{
  fprintf (asm_out_file, "\t.short\t0xa\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_POINTER);
  fprintf (asm_out_file, "\t.short\t0x%x\n", ptr->type);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.long\t0x%x\n", ptr->attr.num);
}

static void
write_array(struct pdb_array *arr)
{
  fprintf (asm_out_file, "\t.short\t0xe\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_ARRAY);

  fprintf (asm_out_file, "\t.short\t0x%x\n", arr->type);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.short\t0x%x\n", arr->index_type);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.short\t0x%x\n", arr->length);

  fprintf (asm_out_file, "\t.byte\t0\n"); // empty string
  fprintf (asm_out_file, "\t.byte\t0xf1\n");
}

static void
write_arglist(struct pdb_arglist *arglist)
{
  unsigned int len = 8 + (4 * arglist->count);

  if (arglist->count == 0) // zero-length arglist has dummy entry
    len += 4;

  fprintf (asm_out_file, "\t.short\t0x%x\n", len - 2);
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_ARGLIST);
  fprintf (asm_out_file, "\t.short\t0x%x\n", arglist->count == 0 ? 1 : arglist->count);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding

  for (unsigned int i = 0; i < arglist->count; i++) {
    fprintf (asm_out_file, "\t.short\t0x%x\n", arglist->args[i]);
    fprintf (asm_out_file, "\t.short\t0\n"); // padding
  }

  if (arglist->count == 0) {
    fprintf (asm_out_file, "\t.short\t0\n"); // empty type
    fprintf (asm_out_file, "\t.short\t0\n"); // padding
  }
}

static void
write_procedure(struct pdb_proc *proc)
{
  fprintf (asm_out_file, "\t.short\t0xe\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_PROCEDURE);
  fprintf (asm_out_file, "\t.short\t0x%x\n", proc->return_type);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.byte\t0x%x\n", proc->calling_convention);
  fprintf (asm_out_file, "\t.byte\t0x%x\n", proc->attributes);
  fprintf (asm_out_file, "\t.short\t0x%x\n", proc->num_args);
  fprintf (asm_out_file, "\t.short\t0x%x\n", proc->arg_list);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
}

static void
write_type(struct pdb_type *t)
{
  switch (t->cv_type) {
    case CODEVIEW_LF_FIELDLIST:
      write_fieldlist((struct pdb_fieldlist*)t->data);
      break;

    case CODEVIEW_LF_CLASS:
    case CODEVIEW_LF_STRUCTURE:
      write_struct(t->cv_type, (struct pdb_struct*)t->data);
      break;

    case CODEVIEW_LF_UNION:
      write_union((struct pdb_struct*)t->data);
      break;

    case CODEVIEW_LF_ENUM:
      write_enum((struct pdb_enum*)t->data);
      break;

    case CODEVIEW_LF_POINTER:
      write_pointer((struct pdb_pointer*)t->data);
      break;

    case CODEVIEW_LF_ARRAY:
      write_array((struct pdb_array*)t->data);
      break;

    case CODEVIEW_LF_ARGLIST:
      write_arglist((struct pdb_arglist*)t->data);
      break;

    case CODEVIEW_LF_PROCEDURE:
      write_procedure((struct pdb_proc*)t->data);
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
  f->type = find_type(TREE_TYPE(func));

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
  v->type = find_type(TREE_TYPE(var));

  global_vars = v;
}

static uint16_t
add_type(struct pdb_type *t) {
  struct pdb_type *t2 = types;

  // check for dupes

  while (t2) {
    if (t2->cv_type == t->cv_type) {
      switch (t2->cv_type) {
	case CODEVIEW_LF_FIELDLIST:
	{
	  struct pdb_fieldlist *fl1 = (struct pdb_fieldlist*)t->data;
	  struct pdb_fieldlist *fl2 = (struct pdb_fieldlist*)t2->data;

	  if (fl1->count == fl2->count) {
	    bool same = true;

	    for (unsigned int i = 0; i < fl1->count; i++) {
	      if (fl1->entries[i].cv_type != fl2->entries[i].cv_type) {
		same = false;
		break;
	      }

	      if (fl1->entries[i].cv_type == CODEVIEW_LF_MEMBER) {
		if (fl1->entries[i].type != fl2->entries[i].type ||
		    fl1->entries[i].offset != fl2->entries[i].offset ||
		    fl1->entries[i].fld_attr != fl2->entries[i].fld_attr ||
		    strcmp(fl1->entries[i].name, fl2->entries[i].name)) {
		  same = false;
		  break;
		}
	      } else if (fl1->entries[i].cv_type == CODEVIEW_LF_ENUMERATE) {
		if (fl1->entries[i].value != fl2->entries[i].value ||
		    strcmp(fl1->entries[i].name, fl2->entries[i].name)) {
		  same = false;
		  break;
		}
	      }
	    }

	    if (same) {
	      for (unsigned int i = 0; i < fl1->count; i++) {
		if (fl1->entries[i].name)
		  free(fl1->entries[i].name);
	      }

	      free(t);

	      return t2->id;
	    }
	  }

	  break;
	}

	case CODEVIEW_LF_STRUCTURE:
	case CODEVIEW_LF_CLASS:
	case CODEVIEW_LF_UNION:
	{
	  struct pdb_struct *str1 = (struct pdb_struct*)t->data;
	  struct pdb_struct *str2 = (struct pdb_struct*)t2->data;

	  if (str1->count == str2->count &&
	      str1->field == str2->field &&
	      str1->size == str2->size &&
	      !strcmp(str1->name, str2->name)) {
	    if (str1->name)
	      free(str1->name);

	    free(t);

	    return t2->id;
	  }

	  break;
	}

	case CODEVIEW_LF_ENUM:
	{
	  struct pdb_enum *en1 = (struct pdb_enum*)t->data;
	  struct pdb_enum *en2 = (struct pdb_enum*)t2->data;

	  if (en1->count == en2->count &&
	      en1->type == en2->type &&
	      en1->field == en2->field &&
	      !strcmp(en1->name, en2->name)) {
	    if (en1->name)
	      free(en1->name);

	    free(t);

	    return t2->id;
	  }

	  break;
	}

	case CODEVIEW_LF_POINTER:
	{
	  struct pdb_pointer *ptr1 = (struct pdb_pointer*)t->data;
	  struct pdb_pointer *ptr2 = (struct pdb_pointer*)t2->data;

	  if (ptr1->type == ptr2->type &&
	      ptr1->attr.num == ptr2->attr.num) {
	    free(t);

	    return t2->id;
	  }

	  break;
	}

	case CODEVIEW_LF_ARRAY:
	{
	  struct pdb_array *arr1 = (struct pdb_array*)t->data;
	  struct pdb_array *arr2 = (struct pdb_array*)t2->data;

	  if (arr1->type == arr2->type &&
	      arr1->index_type == arr2->index_type &&
	      arr1->length == arr2->length) {
	    free(t);

	    return t2->id;
	  }

	  break;
	}

	case CODEVIEW_LF_ARGLIST:
	{
	  struct pdb_arglist *arglist1 = (struct pdb_arglist*)t->data;
	  struct pdb_arglist *arglist2 = (struct pdb_arglist*)t2->data;

	  if (arglist1->count == arglist2->count) {
	    bool same = true;

	    for (unsigned int i = 0; i < arglist1->count; i++) {
	      if (arglist1->args[i] != arglist2->args[i]) {
		same = false;
		break;
	      }
	    }

	    if (same) {
	      free(t);

	      return t2->id;
	    }
	  }

	  break;
	}

	case CODEVIEW_LF_PROCEDURE:
	{
	  struct pdb_proc *proc1 = (struct pdb_proc*)t->data;
	  struct pdb_proc *proc2 = (struct pdb_proc*)t2->data;

	  if (proc1->return_type == proc2->return_type &&
	      proc1->calling_convention == proc2->calling_convention &&
	      proc1->attributes == proc2->attributes &&
	      proc1->num_args == proc2->num_args &&
	      proc1->arg_list == proc2->arg_list) {
	    free(t);

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

  t->tree = NULL;

  if (last_type)
    last_type->next = t;

  if (!types)
    types = t;

  last_type = t;

  return t->id;
}

static uint16_t
find_type_struct(tree t)
{
  tree f;
  struct pdb_type *fltype, *strtype;
  struct pdb_fieldlist *fieldlist;
  struct pdb_fieldlist_entry *ent;
  struct pdb_struct *str;
  unsigned int num_entries = 0;
  uint16_t fltypenum;

  // FIXME - what about self-referencing structs?

  f = t->type_non_common.values;

  while (f) {
    if (TREE_CODE(f) == FIELD_DECL)
      num_entries++;

    f = f->common.chain;
  }

  // add fieldlist type

  fltype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_fieldlist));
  fltype->cv_type = CODEVIEW_LF_FIELDLIST;
  fltype->tree = NULL;

  fieldlist = (struct pdb_fieldlist*)fltype->data;
  fieldlist->count = num_entries;
  fieldlist->entries = (struct pdb_fieldlist_entry*)xmalloc(sizeof(struct pdb_fieldlist_entry) * num_entries);

  ent = fieldlist->entries;
  f = t->type_non_common.values;

  while (f) {
    if (TREE_CODE(f) == FIELD_DECL) {
      unsigned int bit_offset = (TREE_INT_CST_ELT(DECL_FIELD_OFFSET(f), 0) * 8) + TREE_INT_CST_ELT(DECL_FIELD_BIT_OFFSET(f), 0);

      ent->cv_type = CODEVIEW_LF_MEMBER;
      ent->type = find_type(f->common.typed.type);
      ent->offset = bit_offset / 8; // FIXME - what about bit fields?
      ent->fld_attr = CV_FLDATTR_PUBLIC; // FIXME?
      ent->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(f)));
    }

    f = f->common.chain;
    ent++;
  }

  fltypenum = add_type(fltype);

  // add type for struct

  strtype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_struct));
  strtype->cv_type = CODEVIEW_LF_STRUCTURE; // FIXME - LF_CLASS if C++ class?
  strtype->tree = t;

  str = (struct pdb_struct*)strtype->data;
  str->count = num_entries;
  str->field = fltypenum;
  str->size = TREE_INT_CST_ELT(TYPE_SIZE(t), 0) / 8;

  if (TREE_CODE(TYPE_NAME(t)) == IDENTIFIER_NODE)
    str->name = xstrdup(IDENTIFIER_POINTER(TYPE_NAME(t)));
  else if (TREE_CODE(TYPE_NAME(t)) == TYPE_DECL)
    str->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(t))));
  else
    str->name = NULL;

  return add_type(strtype);
}

static uint16_t
find_type_union(tree t)
{
  tree f;
  struct pdb_type *fltype, *uniontype;
  struct pdb_fieldlist *fieldlist;
  struct pdb_fieldlist_entry *ent;
  struct pdb_struct *str;
  unsigned int num_entries = 0;
  uint16_t fltypenum;

  f = t->type_non_common.values;

  while (f) {
    if (TREE_CODE(f) == FIELD_DECL)
      num_entries++;

    f = f->common.chain;
  }

  // add fieldlist type

  fltype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_fieldlist));
  fltype->cv_type = CODEVIEW_LF_FIELDLIST;
  fltype->tree = NULL;

  fieldlist = (struct pdb_fieldlist*)fltype->data;
  fieldlist->count = num_entries;
  fieldlist->entries = (struct pdb_fieldlist_entry*)xmalloc(sizeof(struct pdb_fieldlist_entry) * num_entries);

  ent = fieldlist->entries;
  f = t->type_non_common.values;

  while (f) {
    if (TREE_CODE(f) == FIELD_DECL) {
      unsigned int bit_offset = (TREE_INT_CST_ELT(DECL_FIELD_OFFSET(f), 0) * 8) + TREE_INT_CST_ELT(DECL_FIELD_BIT_OFFSET(f), 0);

      ent->cv_type = CODEVIEW_LF_MEMBER;
      ent->type = find_type(f->common.typed.type);
      ent->offset = bit_offset / 8; // FIXME - what about bit fields?
      ent->fld_attr = CV_FLDATTR_PUBLIC; // FIXME?
      ent->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(f)));
    }

    f = f->common.chain;
    ent++;
  }

  fltypenum = add_type(fltype);

  // add type for union

  uniontype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_struct));
  uniontype->cv_type = CODEVIEW_LF_UNION;
  uniontype->tree = t;

  str = (struct pdb_struct*)uniontype->data;
  str->count = num_entries;
  str->field = fltypenum;
  str->size = TREE_INT_CST_ELT(TYPE_SIZE(t), 0) / 8;

  if (TREE_CODE(TYPE_NAME(t)) == IDENTIFIER_NODE)
    str->name = xstrdup(IDENTIFIER_POINTER(TYPE_NAME(t)));
  else if (TREE_CODE(TYPE_NAME(t)) == TYPE_DECL)
    str->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(t))));
  else
    str->name = NULL;

  return add_type(uniontype);
}

static uint16_t
find_type_enum(tree t)
{
  tree v;
  struct pdb_type *fltype, *enumtype;
  struct pdb_fieldlist *fieldlist;
  struct pdb_fieldlist_entry *ent;
  struct pdb_enum *en;
  unsigned int num_entries, size;
  uint16_t fltypenum;

  v = TYPE_VALUES(t);
  num_entries = 0;

  while (v) {
    num_entries++;

    v = v->common.chain;
  }

  // add fieldlist type

  fltype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_fieldlist));
  fltype->cv_type = CODEVIEW_LF_FIELDLIST;
  fltype->tree = NULL;

  fieldlist = (struct pdb_fieldlist*)fltype->data;
  fieldlist->count = num_entries;
  fieldlist->entries = (struct pdb_fieldlist_entry*)xmalloc(sizeof(struct pdb_fieldlist_entry) * num_entries);

  ent = fieldlist->entries;
  v = TYPE_VALUES(t);

  while (v) {
    ent->cv_type = CODEVIEW_LF_ENUMERATE;
    ent->value = TREE_INT_CST_ELT(TREE_VALUE(v), 0);
    ent->name = xstrdup(IDENTIFIER_POINTER(TREE_PURPOSE(v)));

    v = v->common.chain;
    ent++;
  }

  fltypenum = add_type(fltype);

  // add type for enum

  enumtype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_enum));
  enumtype->cv_type = CODEVIEW_LF_ENUM;
  enumtype->tree = t;

  en = (struct pdb_enum*)enumtype->data;
  en->count = num_entries;
  en->field = fltypenum;

  size = TREE_INT_CST_ELT(TYPE_SIZE(t), 0);

  if (size == 8)
    en->type = TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_BYTE : CV_BUILTIN_TYPE_SBYTE;
  else if (size == 16)
    en->type = TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT16 : CV_BUILTIN_TYPE_INT16;
  else if (size == 32)
    en->type = TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT32 : CV_BUILTIN_TYPE_INT32;
  else if (size == 64)
    en->type = TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT64 : CV_BUILTIN_TYPE_INT64;
  else
    en->type = 0;

  if (TREE_CODE(TYPE_NAME(t)) == IDENTIFIER_NODE)
    en->name = xstrdup(IDENTIFIER_POINTER(TYPE_NAME(t)));
  else if (TREE_CODE(TYPE_NAME(t)) == TYPE_DECL)
    en->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(t))));
  else
    en->name = NULL;

  return add_type(enumtype);
}

static uint16_t
find_type_pointer(tree t)
{
  struct pdb_type *ptrtype;
  struct pdb_pointer *ptr;
  unsigned int size = TREE_INT_CST_ELT(TYPE_SIZE(t), 0) / 8;
  uint16_t type = find_type(TREE_TYPE(t));

  if (type == 0)
    return 0;

  if (type < FIRST_TYPE_NUM) { // pointers to builtins have their own constants
    if (size == 4)
      return (CV_TM_NPTR32 << 8) | type;
    else if (size == 8)
      return (CV_TM_NPTR64 << 8) | type;
  }

  ptrtype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_pointer));
  ptrtype->cv_type = CODEVIEW_LF_POINTER;
  ptrtype->tree = t;

  ptr = (struct pdb_pointer*)ptrtype->data;
  ptr->type = type;
  ptr->attr.num = 0;

  ptr->attr.s.size = size;

  if (size == 8)
    ptr->attr.s.ptrtype = CV_PTR_64;
  else if (size == 4)
    ptr->attr.s.ptrtype = CV_PTR_NEAR32;

  // FIXME - const and volatile pointers
  // FIXME - C++ references

  return add_type(ptrtype);
}

static uint16_t
find_type_array(tree t)
{
  struct pdb_type *arrtype;
  struct pdb_array *arr;
  uint16_t type = find_type(TREE_TYPE(t));

  if (type == 0)
    return 0;

  arrtype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_array));
  arrtype->cv_type = CODEVIEW_LF_ARRAY;
  arrtype->tree = t;

  arr = (struct pdb_array*)arrtype->data;
  arr->type = type;
  arr->index_type = CV_BUILTIN_TYPE_UINT32LONG; // FIXME?
  arr->length = TREE_INT_CST_ELT(TYPE_SIZE(t), 0) / 8;

  return add_type(arrtype);
}

static uint16_t
find_type_function(tree t)
{
  struct pdb_type *arglisttype, *proctype;
  struct pdb_arglist *arglist;
  struct pdb_proc *proc;
  tree arg;
  unsigned int num_args = 0;
  uint16_t *argptr;
  uint16_t arglisttypenum;

  // create arglist

  arg = TYPE_ARG_TYPES(t);
  while (arg) {
    if (TREE_VALUE(arg)->base.code != VOID_TYPE)
      num_args++;

    arg = arg->common.chain;
  }

  arglisttype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + offsetof(struct pdb_arglist, args) + (num_args * sizeof(uint16_t)));
  arglisttype->cv_type = CODEVIEW_LF_ARGLIST;
  arglisttype->tree = NULL;

  arglist = (struct pdb_arglist*)arglisttype->data;
  arglist->count = num_args;

  argptr = arglist->args;
  arg = TYPE_ARG_TYPES(t);
  while (arg) {
    if (TREE_VALUE(arg)->base.code != VOID_TYPE) {
      *argptr = find_type(TREE_VALUE(arg));
      argptr++;
    }

    arg = arg->common.chain;
  }

  arglisttypenum = add_type(arglisttype);

  // create procedure

  proctype = (struct pdb_type *)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_proc));
  proctype->cv_type = CODEVIEW_LF_PROCEDURE;
  proctype->tree = t;

  proc = (struct pdb_proc*)proctype->data;

  proc->return_type = find_type(TREE_TYPE(t));
  proc->attributes = 0;
  proc->num_args = num_args;
  proc->arg_list = arglisttypenum;

  if (TARGET_64BIT)
    proc->calling_convention = CV_CALL_NEAR_C;
  else {
    switch (ix86_get_callcvt(t)) {
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

  return add_type(proctype);
}

static uint16_t
find_type(tree t)
{
  struct pdb_type *type;

  if (!t)
    return 0;

  if (t->base.code == INTEGER_TYPE) {
    unsigned int size;

    // FIXME - constness?

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

    // FIXME - should we be mapping long long to Int64Quad? (What does MSVC do?)

    size = TREE_INT_CST_ELT(TYPE_SIZE(t), 0);

    if (size == 8)
      return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_BYTE : CV_BUILTIN_TYPE_SBYTE;
    else if (size == 16)
      return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT16 : CV_BUILTIN_TYPE_INT16;
    else if (size == 32)
      return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT32 : CV_BUILTIN_TYPE_INT32;
    else if (size == 64)
      return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT64 : CV_BUILTIN_TYPE_INT64;

    // FIXME - HRESULT
    // FIXME - 128-bit integers?

    return 0;
  } else if (t->base.code == VOID_TYPE)
    return CV_BUILTIN_TYPE_VOID;

  // search through existing types

  type = types;
  while (type) {
    if (type->tree == t)
      return type->id;

    type = type->next;
  }

  // FIXME - wchar_t, char16_t, char32_t, char8_t
  // FIXME - real_type
  // FIXME - complex types
  // FIXME - booleans
  // FIXME - C++ references
  // FIXME - constness etc.
  // FIXME - any others?

  switch (t->base.code) {
    case POINTER_TYPE:
      return find_type_pointer(t);

    case ARRAY_TYPE:
      return find_type_array(t);

    case RECORD_TYPE:
      return find_type_struct(t);

    case UNION_TYPE:
      return find_type_union(t);

    case ENUMERAL_TYPE:
      return find_type_enum(t);

    case FUNCTION_TYPE:
      return find_type_function(t);

    default:
      return 0;
  }
}

static void pdbout_type_decl(tree t, int local ATTRIBUTE_UNUSED)
{
  if (DECL_IN_SYSTEM_HEADER(t)) // ignoring system headers for now (FIXME)
    return;

  // FIXME - if from file in /usr/include or wherever, only include in output if used

  find_type(t->typed.type);
}
