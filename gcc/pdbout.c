#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "debug.h"

const struct gcc_debug_hooks pdb_debug_hooks =
{
  debug_nothing_charstar,
  debug_nothing_charstar,
  debug_nothing_charstar,			/* early_finish */
  debug_nothing_void,
  debug_nothing_int_charstar,
  debug_nothing_int_charstar,
  debug_nothing_int_charstar,
  debug_nothing_int,
  debug_nothing_int_int,	         /* begin_block */
  debug_nothing_int_int,	         /* end_block */
  debug_true_const_tree,	         /* ignore_block */
  debug_nothing_int_int_charstar_int_bool, /* source_line */
  debug_nothing_int_int_charstar,	 /* begin_prologue */
  debug_nothing_int_charstar,	         /* end_prologue */
  debug_nothing_int_charstar,	         /* begin_epilogue */
  debug_nothing_int_charstar,	         /* end_epilogue */
  debug_nothing_tree,		         /* begin_function */
  debug_nothing_int,		         /* end_function */
  debug_nothing_tree,		         /* register_main_translation_unit */
  debug_nothing_tree,		         /* function_decl */
  debug_nothing_tree,	         	 /* early_global_decl */
  debug_nothing_tree,	         	 /* late_global_decl */
  debug_nothing_tree_int,		 /* type_decl */
  debug_nothing_tree_tree_tree_bool_bool,/* imported_module_or_decl */
  debug_false_tree_charstarstar_uhwistar,/* die_ref_for_decl */
  debug_nothing_tree_charstar_uhwi,      /* register_external_die */
  debug_nothing_tree,		         /* deferred_inline_function */
  debug_nothing_tree,		         /* outlining_inline_function */
  debug_nothing_rtx_code_label,	         /* label */
  debug_nothing_int,		         /* handle_pch */
  debug_nothing_rtx_insn,	         /* var_location */
  debug_nothing_tree,	         	 /* inline_entry */
  debug_nothing_tree,			 /* size_function */
  debug_nothing_void,                    /* switch_text_section */
  debug_nothing_tree_tree,		 /* set_name */
  0,                                     /* start_end_main_source_file */
  TYPE_SYMTAB_IS_ADDRESS                 /* tree_type_symtab_field */
};
