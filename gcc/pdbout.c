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
#include "defaults.h"
#include "config/i386/i386-protos.h"
#include "md5.h"
#include "rtl.h"
#include "insn-config.h"
#include "reload.h"
#include "print-tree.h" // FIXME - remove this
#include "print-rtl.h" // FIXME - and this

#define FUNC_BEGIN_LABEL	".LFB"
#define FUNC_END_LABEL		".LFE"

#define FIRST_TYPE_NUM		0x1000

static const char unnamed[] = "<unnamed-tag>";

static void pdbout_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
				   unsigned int column ATTRIBUTE_UNUSED,
				   const char *file ATTRIBUTE_UNUSED);
static void pdbout_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
				 const char *file ATTRIBUTE_UNUSED);
static void pdbout_init (const char *filename);
static void pdbout_finish (const char *filename);
static void pdbout_begin_function(tree func);
static void pdbout_late_global_decl(tree var);
static void pdbout_type_decl(tree t, int local ATTRIBUTE_UNUSED);
static void pdbout_start_source_file(unsigned int line ATTRIBUTE_UNUSED, const char *file);
static void pdbout_source_line(unsigned int line, unsigned int column ATTRIBUTE_UNUSED,
			       const char *text ATTRIBUTE_UNUSED, int discriminator ATTRIBUTE_UNUSED,
			       bool is_stmt ATTRIBUTE_UNUSED);
static void pdbout_function_decl(tree decl);

static uint16_t find_type(tree t);

static struct pdb_func *funcs = NULL, *cur_func = NULL;
static struct pdb_global_var *global_vars = NULL;
static struct pdb_type *types = NULL, *last_type = NULL;
static struct pdb_alias *aliases = NULL;
static uint16_t type_num = FIRST_TYPE_NUM;
static struct pdb_source_file *source_files = NULL, *last_source_file = NULL;
static uint32_t source_file_string_offset = 1;
static unsigned int num_line_number_entries = 0;
static unsigned int num_source_files = 0;

const struct gcc_debug_hooks pdb_debug_hooks =
{
  pdbout_init,
  pdbout_finish,
  debug_nothing_charstar,		 /* early_finish */
  debug_nothing_void,			 /* assembly_start */
  debug_nothing_int_charstar,		 /* define */
  debug_nothing_int_charstar,		 /* undef */
  pdbout_start_source_file,
  debug_nothing_int,			 /* end_source_file */
  debug_nothing_int_int,	         /* begin_block */
  debug_nothing_int_int,	         /* end_block */
  debug_true_const_tree,	         /* ignore_block */
  pdbout_source_line,
  pdbout_begin_prologue,
  debug_nothing_int_charstar,	         /* end_prologue */
  debug_nothing_int_charstar,	         /* begin_epilogue */
  pdbout_end_epilogue,		         /* end_epilogue */
  pdbout_begin_function,
  debug_nothing_int,			 /* end_function */
  debug_nothing_tree,			 /* register_main_translation_unit */
  pdbout_function_decl,
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
pdbout_local_variable (struct pdb_local_var *v)
{
  uint16_t len, align;
  size_t name_len = strlen(v->name);

  if (v->var_type == pdb_local_var_regrel) {
    if (v->reg == CV_X86_EBP) { // ebp is a special case
      len = 13 + name_len;

      if (len % 4 != 0) {
	align = 4 - (len % 4);
	len += 4 - (len % 4);
      } else
	align = 0;

      fprintf (asm_out_file, "\t.short\t0x%x\n", (uint16_t)(len - sizeof(uint16_t))); // reclen
      fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_S_BPREL32);
      fprintf (asm_out_file, "\t.long\t0x%x\n", v->offset);
      fprintf (asm_out_file, "\t.long\t0x%x\n", v->type);

      ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);
    } else {
      len = 15 + name_len;

      if (len % 4 != 0) {
	align = 4 - (len % 4);
	len += 4 - (len % 4);
      } else
	align = 0;

      fprintf (asm_out_file, "\t.short\t0x%x\n", (uint16_t)(len - sizeof(uint16_t))); // reclen
      fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_S_REGREL32);
      fprintf (asm_out_file, "\t.long\t0x%x\n", v->offset);
      fprintf (asm_out_file, "\t.long\t0x%x\n", v->type);
      fprintf (asm_out_file, "\t.short\t0x%x\n", v->reg);

      ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);
    }

    for (unsigned int i = 0; i < align; i++) {
      fprintf (asm_out_file, "\t.byte\t0\n");
    }
  } else if (v->var_type == pdb_local_var_register) {
    len = 11 + name_len;

    if (len % 4 != 0) {
      align = 4 - (len % 4);
      len += 4 - (len % 4);
    } else
      align = 0;

    fprintf (asm_out_file, "\t.short\t0x%x\n", (uint16_t)(len - sizeof(uint16_t))); // reclen
    fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_S_REGISTER);
    fprintf (asm_out_file, "\t.long\t0x%x\n", v->type);
    fprintf (asm_out_file, "\t.short\t0x%x\n", v->reg);

    ASM_OUTPUT_ASCII (asm_out_file, v->name, name_len + 1);

    for (unsigned int i = 0; i < align; i++) {
      fprintf (asm_out_file, "\t.byte\t0\n");
    }
  }
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
  fprintf (asm_out_file, "\t.long\t[.cvprocend%u]-[.debug$S]\n", func->num); // pEnd
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

  for (unsigned int i = 0; i < align; i++) {
    fprintf (asm_out_file, "\t.byte\t0\n");
  }

  // FIXME - S_FRAMEPROC, S_CALLSITEINFO, etc.

  // locals

  while (func->local_vars) {
    struct pdb_local_var *n = func->local_vars->next;

    pdbout_local_variable(func->local_vars);

    free(func->local_vars);

    func->local_vars = n;
  }

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
write_file_checksums()
{
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_DEBUG_S_FILECHKSMS);
  fprintf (asm_out_file, "\t.long\t[.chksumsend]-[.chksumsstart]\n");
  fprintf (asm_out_file, ".chksumsstart:\n");

  while (source_files) {
    struct pdb_source_file *n;

    fprintf (asm_out_file, "\t.long\t0x%x\n", source_files->str_offset);
    fprintf (asm_out_file, "\t.byte\t0x%x\n", 16); // length of MD5 hash
    fprintf (asm_out_file, "\t.byte\t0x%x\n", CV_CHKSUM_TYPE_CHKSUM_TYPE_MD5);

    for (unsigned int i = 0; i < 16; i++) {
      fprintf (asm_out_file, "\t.byte\t0x%x\n", source_files->hash[i]);
    }

    fprintf (asm_out_file, "\t.short\t0\n");

    n = source_files->next;

    free(source_files);

    source_files = n;
  }

  fprintf (asm_out_file, ".chksumsend:\n");
}

static void
write_line_numbers()
{
  struct pdb_func *func = funcs;

  while (func) {
    struct pdb_line *l;
    unsigned int num_entries = 0;

    if (!func->lines) {
      func = func->next;
      continue;
    }

    l = func->lines;
    while (l) {
      num_entries++;
      l = l->next;
    }

    fprintf (asm_out_file, "\t.long\t0x%x\n", CV_DEBUG_S_LINES);
    fprintf (asm_out_file, "\t.long\t[.linesend%u]-[.linesstart%u]\n", func->num, func->num);
    fprintf (asm_out_file, ".linesstart%u:\n", func->num);

    fprintf (asm_out_file, "\t.long\t[" FUNC_BEGIN_LABEL "%u]\n", func->num); // address
    fprintf (asm_out_file, "\t.short\t0\n"); // segment (filled in by linker)
    fprintf (asm_out_file, "\t.short\t0\n"); // flags
    fprintf (asm_out_file, "\t.long\t[" FUNC_END_LABEL "%u]-[" FUNC_BEGIN_LABEL "%u]\n", func->num, func->num); // length

    fprintf (asm_out_file, "\t.long\t0x%x\n", func->source_file * 0x18); // file ID (0x18 is size of checksum struct)
    fprintf (asm_out_file, "\t.long\t0x%x\n", num_entries);
    fprintf (asm_out_file, "\t.long\t0x%x\n", 0xc + (num_entries * 8)); // length of file block

    l = func->lines;
    while (l) {
      fprintf (asm_out_file, "\t.long\t[.line%u]-[" FUNC_BEGIN_LABEL "%u]\n", l->entry, func->num); // offset
      fprintf (asm_out_file, "\t.long\t0x%x\n", l->line); // line no.

      l = l->next;
    }

    while (func->lines) {
      struct pdb_line *n = func->lines->next;

      free(func->lines);

      func->lines = n;
    }

    fprintf (asm_out_file, ".linesend%u:\n", func->num);

    func = func->next;
  }
}

static void
write_pdb_section()
{
  struct pdb_source_file *psf;
  struct pdb_func *func;

  fprintf (asm_out_file, "\t.section\t.debug$S, \"ndr\"\n");
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_SIGNATURE_C13);
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_DEBUG_S_SYMBOLS);
  fprintf (asm_out_file, "\t.long\t[.symend]-[.symstart]\n");

  fprintf (asm_out_file, ".symstart:\n");

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

  func = funcs;
  while (func) {
    pdbout_proc32(func);

    func = func->next;
  }

  fprintf (asm_out_file, ".symend:\n");

  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_DEBUG_S_STRINGTABLE);
  fprintf (asm_out_file, "\t.long\t[.strtableend]-[.strtablestart]\n");
  fprintf (asm_out_file, ".strtablestart:\n");
  fprintf (asm_out_file, "\t.byte\t0\n");

  psf = source_files;
  while (psf) {
    size_t name_len = strlen(psf->name);

    ASM_OUTPUT_ASCII (asm_out_file, psf->name + name_len + 1, strlen(psf->name + name_len + 1) + 1);

    psf = psf->next;
  }

  fprintf (asm_out_file, "\t.balign\t4\n");
  fprintf (asm_out_file, ".strtableend:\n");

  write_file_checksums();

  write_line_numbers();

  while (funcs) {
    struct pdb_func *n = funcs->next;

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
  size_t name_len = str->name ? strlen(str->name) : (sizeof(unnamed) - 1);
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

  if (str->name)
    ASM_OUTPUT_ASCII (asm_out_file, str->name, name_len + 1);
  else
    ASM_OUTPUT_ASCII (asm_out_file, unnamed, sizeof(unnamed));

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
  size_t name_len = str->name ? strlen(str->name) : (sizeof(unnamed) - 1);
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

  if (str->name)
    ASM_OUTPUT_ASCII (asm_out_file, str->name, name_len + 1);
  else
    ASM_OUTPUT_ASCII (asm_out_file, unnamed, sizeof(unnamed));

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
  fprintf (asm_out_file, "\t.long\t0x%x\n", arglist->count == 0 ? 1 : arglist->count);

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
write_string_id(struct pdb_type *t)
{
  size_t string_len = strlen((const char*)t->data);
  size_t len = 9 + string_len, align;

  if (len % 4 != 0)
    align = 4 - (len % 4);
  else
    align = 0;

  len += align;

  fprintf (asm_out_file, "\t.short\t0x%x\n", (uint16_t)(len - sizeof(uint16_t)));
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_STRING_ID);
  fprintf (asm_out_file, "\t.long\t0\n");
  ASM_OUTPUT_ASCII (asm_out_file, (const char*)t->data, string_len + 1);

  if (align == 3)
    fprintf (asm_out_file, "\t.byte\t0xf3\n");

  if (align >= 2)
    fprintf (asm_out_file, "\t.byte\t0xf2\n");

  if (align >= 1)
    fprintf (asm_out_file, "\t.byte\t0xf1\n");
}

static void
write_udt_src_line(struct pdb_udt_src_line *t)
{
  fprintf (asm_out_file, "\t.short\t0xe\n");
  fprintf (asm_out_file, "\t.short\t0x%x\n", CODEVIEW_LF_UDT_SRC_LINE);
  fprintf (asm_out_file, "\t.short\t0x%x\n", t->type);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.short\t0x%x\n", t->source_file);
  fprintf (asm_out_file, "\t.short\t0\n"); // padding
  fprintf (asm_out_file, "\t.long\t0x%x\n", t->line);
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

    case CODEVIEW_LF_STRING_ID:
      write_string_id(t);
      break;

    case CODEVIEW_LF_UDT_SRC_LINE:
      write_udt_src_line((struct pdb_udt_src_line*)t->data);
      break;
  }
}

static void
write_pdb_type_section()
{
  fprintf (asm_out_file, "\t.section\t.debug$T, \"ndr\"\n");
  fprintf (asm_out_file, "\t.long\t0x%x\n", CV_SIGNATURE_C13);

  while (types) {
    struct pdb_type *n;

    write_type(types);

    n = types->next;

    free_type(types);

    types = n;
  }

  while (aliases) {
    struct pdb_alias *n;

    n = aliases->next;

    free(aliases);

    aliases = n;
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
  expanded_location xloc;
  struct pdb_source_file *psf;
  struct pdb_func *f = (struct pdb_func*)xmalloc(sizeof(struct pdb_func));

  f->next = funcs;
  f->name = xstrdup(IDENTIFIER_POINTER(DECL_NAME(func)));
  f->num = current_function_funcdef_no;
  f->public_flag = func->base.public_flag;
  f->type = find_type(TREE_TYPE(func));
  f->lines = f->last_line = NULL;
  f->local_vars = f->last_local_var = NULL;

  funcs = f;

  cur_func = f;

  xloc = expand_location(DECL_SOURCE_LOCATION(func));

  f->source_file = 0;

  psf = source_files;
  while (psf) {
    if (!strcmp(xloc.file, psf->name)) {
      f->source_file = psf->num;
      break;
    }

    psf = psf->next;
  }

  if (xloc.line != 0)
    pdbout_source_line(xloc.line, 0, NULL, 0, 0);
}

static void pdbout_late_global_decl(tree var)
{
  if (TREE_CODE (var) != VAR_DECL)
    return;

  if (!DECL_ASSEMBLER_NAME_RAW (var))
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
	      ((!str1->name && !str2->name) || (str1->name && str2->name && !strcmp(str1->name, str2->name)))) {
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

	case CODEVIEW_LF_STRING_ID:
	  if (!strcmp((const char*)t->data, (const char*)t2->data)) {
	    free(t);

	    return t2->id;
	  }
	break;

	case CODEVIEW_LF_UDT_SRC_LINE:
	  if (!memcmp(t->data, t2->data, sizeof(struct pdb_udt_src_line))) {
	    free(t);

	    return t2->id;
	  }
	break;
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

      ent++;
    }

    f = f->common.chain;
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

  if (TYPE_NAME(t) && TREE_CODE(TYPE_NAME(t)) == IDENTIFIER_NODE)
    str->name = xstrdup(IDENTIFIER_POINTER(TYPE_NAME(t)));
  else if (TYPE_NAME(t) && TREE_CODE(TYPE_NAME(t)) == TYPE_DECL)
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
      ent++;
    }

    f = f->common.chain;
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

  if (TYPE_NAME(t) && TREE_CODE(TYPE_NAME(t)) == IDENTIFIER_NODE)
    str->name = xstrdup(IDENTIFIER_POINTER(TYPE_NAME(t)));
  else if (TYPE_NAME(t) && TREE_CODE(TYPE_NAME(t)) == TYPE_DECL)
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
    ent->fld_attr = 0; // FIXME
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
  struct pdb_alias *a;

  if (!t)
    return 0;

  // search through typedefs

  a = aliases;
  while (a) {
    if (a->tree == t)
      return a->cv_type;

    a = a->next;
  }

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

    switch (size) {
      case 8:
	return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_BYTE : CV_BUILTIN_TYPE_SBYTE;

      case 16:
	if (!strcmp(IDENTIFIER_POINTER(TYPE_IDENTIFIER(t)), "wchar_t"))
	  return CV_BUILTIN_TYPE_WIDE_CHARACTER;

	return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT16 : CV_BUILTIN_TYPE_INT16;

      case 32:
	return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT32 : CV_BUILTIN_TYPE_INT32;

      case 64:
	return TYPE_UNSIGNED(t) ? CV_BUILTIN_TYPE_UINT64 : CV_BUILTIN_TYPE_INT64;
    }

    // FIXME - HRESULT
    // FIXME - 128-bit integers?

    return 0;
  } else if (t->base.code == REAL_TYPE) {
    unsigned int size = TREE_INT_CST_ELT(TYPE_SIZE(t), 0);

    switch (size) {
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
  } else if (t->base.code == BOOLEAN_TYPE) {
    unsigned int size = TREE_INT_CST_ELT(TYPE_SIZE(t), 0);

    switch (size) {
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
  } else if (t->base.code == VOID_TYPE)
    return CV_BUILTIN_TYPE_VOID;

  // search through existing types

  type = types;
  while (type) {
    if (type->tree == t)
      return type->id;

    type = type->next;
  }

  // FIXME - char16_t, char32_t, char8_t
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

static uint16_t
add_string_type(const char *s)
{
  struct pdb_type *type;
  size_t len = strlen(s);

  type = (struct pdb_type*)xmalloc(offsetof(struct pdb_type, data) + len + 1);
  type->cv_type = CODEVIEW_LF_STRING_ID;
  type->tree = NULL;

  memcpy(type->data, s, len + 1);

  return add_type(type);
}

static uint16_t
add_udt_src_line_type(uint16_t type_id, uint16_t source_file, uint32_t line)
{
  struct pdb_type *type;
  struct pdb_udt_src_line *pusl;

  type = (struct pdb_type*)xmalloc(offsetof(struct pdb_type, data) + sizeof(struct pdb_udt_src_line));
  type->cv_type = CODEVIEW_LF_UDT_SRC_LINE;
  type->tree = NULL;

  pusl = (struct pdb_udt_src_line*)type->data;
  pusl->type = type_id;
  pusl->source_file = source_file;
  pusl->line = line;

  return add_type(type);
}

static void pdbout_type_decl(tree t, int local ATTRIBUTE_UNUSED)
{
  uint16_t type, string_type;
  struct pdb_source_file *psf;
  expanded_location xloc;

  if (DECL_IN_SYSTEM_HEADER(t)) // ignoring system headers for now (FIXME)
    return;

  // FIXME - if from file in /usr/include or wherever, only include in output if used

  if (t->decl_non_common.result) { // typedef
    struct pdb_alias *a;

    a = (struct pdb_alias*)xmalloc(sizeof(struct pdb_alias));

    a->next = aliases;
    a->tree = t->typed.type;
    a->cv_type = find_type(t->decl_non_common.result);

    aliases = a;

    return;
  }

  type = find_type(t->typed.type);

  if (type == 0 || type < FIRST_TYPE_NUM)
    return;

  if (!DECL_SOURCE_LOCATION(t))
    return;

  xloc = expand_location(DECL_SOURCE_LOCATION(t));

  if (!xloc.file)
    return;

  string_type = 0;

  // add filename as LF_STRING_ID, so linker puts it into string table

  psf = source_files;
  while (psf) {
    if (!strcmp(psf->name, xloc.file)) {
      string_type = add_string_type(psf->name + strlen(psf->name) + 1);
      break;
    }

    psf = psf->next;
  }

  // add LF_UDT_SRC_LINE entry, which linker transforms into LF_UDT_MOD_SRC_LINE

  add_udt_src_line_type(type, string_type, xloc.line);
}

static char*
make_windows_path(char *src)
{
  size_t len = strlen(src);
  char *dest = (char*)xmalloc(len + 3);
  char *in, *ptr;

  ptr = dest;
  *ptr = 'Z'; ptr++;
  *ptr = ':'; ptr++;

  in = src;

  for (unsigned int i = 0; i < len; i++) {
    if (*in == '/')
      *ptr = '\\';
    else
      *ptr = *in;

    in++;
    ptr++;
  }

  *ptr = 0;

  free(src);

  return dest;
}

static void
add_source_file(const char *file)
{
  struct pdb_source_file *psf;
  char *path;
  size_t file_len, path_len;
  FILE *f;

  // check not already added
  psf = source_files;
  while (psf) {
    if (!strcmp(psf->name, file))
      return;

    psf = psf->next;
  }

  path = realpath(file, NULL);
  if (!path)
    return;

  path = make_windows_path(path); // FIXME

  file_len = strlen(file);
  path_len = strlen(path);

  f = fopen(file, "r");

  if (!f) {
    free(path);
    return;
  }

  psf = (struct pdb_source_file*)xmalloc(offsetof(struct pdb_source_file, name) + file_len + 1 + path_len + 1);

  md5_stream(f, psf->hash);

  fclose(f);

  psf->next = NULL;
  psf->str_offset = source_file_string_offset;
  memcpy(psf->name, file, file_len + 1);
  memcpy(psf->name + file_len + 1, path, path_len + 1);

  free(path);

  source_file_string_offset += path_len + 1;

  if (last_source_file)
    last_source_file->next = psf;

  last_source_file = psf;

  if (!source_files)
    source_files = psf;

  psf->num = num_source_files;

  num_source_files++;
}

static void
pdbout_start_source_file(unsigned int line ATTRIBUTE_UNUSED, const char *file)
{
  add_source_file(file);
}

static void
pdbout_init(const char *file)
{
  add_source_file(file);
}

static void
pdbout_source_line(unsigned int line, unsigned int column ATTRIBUTE_UNUSED,
			       const char *text ATTRIBUTE_UNUSED, int discriminator ATTRIBUTE_UNUSED,
			       bool is_stmt ATTRIBUTE_UNUSED)
{
  struct pdb_line *ent;

  if (!cur_func)
    return;

  if (cur_func->last_line && cur_func->last_line->line == line)
    return;

  ent = (struct pdb_line*)xmalloc(sizeof(struct pdb_line));

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

static enum pdb_x86_register
map_register_no_x86 (unsigned int regno, machine_mode mode)
{
  if (mode == E_SImode) {
    switch (regno) {
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
  } else if (mode == E_HImode) {
    switch (regno) {
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
  } else if (mode == E_QImode) {
    switch (regno) {
      case AX_REG:
	return CV_X86_AL;

      case DX_REG:
	return CV_X86_DL;

      case CX_REG:
	return CV_X86_CL;

      case BX_REG:
	return CV_X86_BL;
    }
  } else if (mode == E_SFmode || mode == E_DFmode) {
    switch (regno) {
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

  fprintf(stderr, "Unhandled register %x, mode %x\n", regno, mode); // FIXME

  return CV_X86_NONE;
}

static enum pdb_amd64_register
map_register_no_amd64 (unsigned int regno, machine_mode mode)
{
  if (mode == E_SImode) {
    switch (regno) {
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
  } else if (mode == E_DImode) {
    switch (regno) {
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
  } else if (mode == E_HImode) {
    switch (regno) {
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
  } else if (mode == E_QImode) {
    switch (regno) {
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
  } else if (mode == E_SFmode || mode == E_DFmode) {
    switch (regno) {
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

  fprintf(stderr, "Unhandled register %x, mode %x\n", regno, mode); // FIXME

  return CV_AMD64_NONE;
}

static unsigned int
map_register_no (unsigned int regno, machine_mode mode)
{
  // FIXME - check either x86 or amd64

  if (TARGET_64BIT)
    return (unsigned int)map_register_no_amd64(regno, mode);
  else
    return (unsigned int)map_register_no_x86(regno, mode);
}

static void
add_local(const char *name, uint16_t type, rtx rtl)
{
  struct pdb_local_var *plv;
  size_t name_len = strlen(name);

  rtl = eliminate_regs(rtl, VOIDmode, NULL_RTX);

  plv = (struct pdb_local_var*)xmalloc(offsetof(struct pdb_local_var, name) + name_len + 1);
  plv->next = NULL;
  plv->type = type;
  plv->var_type = pdb_local_var_unknown;
  memcpy(plv->name, name, name_len + 1);

  if (rtl->code == MEM) {
    // FIXME - can we have MINUS here instead of PLUS?

    if (rtl->u.fld[0].rt_rtx->code == PLUS && rtl->u.fld[0].rt_rtx->u.fld[0].rt_rtx->code == REG &&
	rtl->u.fld[0].rt_rtx->u.fld[1].rt_rtx->code == CONST_INT) {
      plv->var_type = pdb_local_var_regrel;
      plv->reg = map_register_no(rtl->u.fld[0].rt_rtx->u.fld[0].rt_rtx->u.reg.regno, rtl->u.fld[0].rt_rtx->u.fld[0].rt_rtx->mode) ;
      plv->offset = rtl->u.fld[0].rt_rtx->u.fld[1].rt_rtx->u.fld[0].rt_int;
    } else if (rtl->u.fld[0].rt_rtx->code == REG) {
      plv->var_type = pdb_local_var_regrel;
      plv->reg = map_register_no(rtl->u.fld[0].rt_rtx->u.reg.regno, rtl->u.fld[0].rt_rtx->u.fld[0].rt_rtx->mode);
      plv->offset = 0;
    }
  } else if (rtl->code == REG) {
    plv->var_type = pdb_local_var_register;
    plv->reg = map_register_no(rtl->u.reg.regno, rtl->mode);
  }

  if (plv->var_type == pdb_local_var_unknown) {
    fprintf(stderr, "Unhandled argument: "); // FIXME
    print_rtl(stdout, rtl);
    printf("\n");
  }

  if (cur_func->last_local_var)
    cur_func->last_local_var->next = plv;

  cur_func->last_local_var = plv;

  if (!cur_func->local_vars)
    cur_func->local_vars = plv;
}

static void
pdbout_function_decl(tree decl)
{
  tree f;

  if (!cur_func)
    return;

  // FIXME - blocks
  // FIXME - variable scope

  f = decl->function_decl.arguments;
  while (f) {
    if (TREE_CODE(f) == PARM_DECL && DECL_NAME(f)) {
      add_local(IDENTIFIER_POINTER(DECL_NAME(f)), find_type(f->typed.type),
		f->parm_decl.common.rtl);
    }

    f = f->common.chain;
  }

  f = BLOCK_VARS(DECL_INITIAL(decl));
  while (f) {
    if (TREE_CODE(f) == VAR_DECL && DECL_RTL_SET_P(f)) {
      add_local(IDENTIFIER_POINTER(DECL_NAME(f)), find_type(f->typed.type),
		DECL_RTL(f));
    }

    f = f->common.chain;
  }

  cur_func = NULL;
}
