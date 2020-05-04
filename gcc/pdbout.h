#ifndef GCC_PDBOUT_H
#define GCC_PDBOUT_H 1

#define CODEVIEW_S_END			0x0006
#define CODEVIEW_LF_POINTER		0x1002
#define CODEVIEW_LF_PROCEDURE		0x1008
#define CODEVIEW_S_BPREL32		0x110b
#define CODEVIEW_S_LDATA32		0x110c
#define CODEVIEW_S_GDATA32		0x110d
#define CODEVIEW_S_LPROC32		0x110f
#define CODEVIEW_S_GPROC32		0x1110
#define CODEVIEW_S_REGREL32		0x1111
#define CODEVIEW_LF_ARGLIST		0x1201
#define CODEVIEW_LF_FIELDLIST		0x1203
#define CODEVIEW_LF_ENUMERATE		0x1502
#define CODEVIEW_LF_ARRAY		0x1503
#define CODEVIEW_LF_CLASS		0x1504
#define CODEVIEW_LF_STRUCTURE		0x1505
#define CODEVIEW_LF_UNION		0x1506
#define CODEVIEW_LF_ENUM		0x1507
#define CODEVIEW_LF_MEMBER		0x150d
#define CODEVIEW_LF_STRING_ID		0x1605
#define CODEVIEW_LF_UDT_SRC_LINE	0x1606

#define CV_SIGNATURE_C13	4

#define CV_DEBUG_S_SYMBOLS		0xf1
#define CV_DEBUG_S_LINES		0xf2
#define CV_DEBUG_S_STRINGTABLE		0xf3
#define CV_DEBUG_S_FILECHKSMS		0xf4

#define CV_CHKSUM_TYPE_NONE			0
#define CV_CHKSUM_TYPE_CHKSUM_TYPE_MD5		1
#define CV_CHKSUM_TYPE_CHKSUM_TYPE_SHA1		2
#define CV_CHKSUM_TYPE_CHKSUM_TYPE_SHA_256	3

struct pdb_line {
  struct pdb_line *next;
  unsigned int line;
  unsigned int entry;
};

enum pdb_local_var_type {
  pdb_local_var_unknown,
  pdb_local_var_regrel,
  pdb_local_var_reg
};

struct pdb_local_var {
  struct pdb_local_var *next;
  enum pdb_local_var_type var_type;
  int32_t offset;
  unsigned int reg;
  uint16_t type;
  char name[1];
};

struct pdb_func {
  struct pdb_func *next;
  char *name;
  int num;
  unsigned int public_flag;
  uint16_t type;
  unsigned int source_file;
  struct pdb_line *lines, *last_line;
  struct pdb_local_var *local_vars, *last_local_var;
};

struct pdb_global_var {
  struct pdb_global_var *next;
  char *name;
  char *asm_name;
  unsigned int public_flag;
  uint16_t type;
};

// CV_fldattr_t in cvinfo
#define CV_FLDATTR_PRIVATE	0x0001
#define CV_FLDATTR_PROTECTED	0x0002
#define CV_FLDATTR_PUBLIC	0x0003
#define CV_FLDATTR_VIRTUAL	0x0004
#define CV_FLDATTR_STATIC	0x0008
#define CV_FLDATTR_FRIEND	0x000C
#define CV_FLDATTR_INTRO	0x0010
#define CV_FLDATTR_PUREVIRT	0x0014
#define CV_FLDATTR_PUREINTRO	0x0018
#define CV_FLDATTR_PSEUDO	0x0020
#define CV_FLDATTR_NOINHERIT	0x0040
#define CV_FLDATTR_NOCONSTRUCT	0x0080
#define CV_FLDATTR_COMPGENX	0x0100
#define CV_FLDATTR_SEALED	0x0200

struct pdb_fieldlist_entry {
  uint16_t cv_type;
  uint16_t type;
  uint16_t offset;
  uint16_t fld_attr;
  unsigned int value;
  char* name;
};

struct pdb_fieldlist {
  unsigned int count;
  struct pdb_fieldlist_entry *entries;
};

struct pdb_struct {
  unsigned int count;
  uint16_t field;
  uint16_t size;
  char *name;
};

struct pdb_enum {
  unsigned int count;
  uint16_t type;
  uint16_t field;
  char *name;
};

// from CV_ptrtype_e in cvdump
#define CV_PTR_NEAR32		0x0a
#define CV_PTR_64		0x0c

struct pdb_pointer {
  uint16_t type;
  union {
    struct {
      uint32_t ptrtype : 5;
      uint32_t ptrmode : 3;
      uint32_t isflat32 : 1;
      uint32_t isvolatile : 1;
      uint32_t isconst : 1;
      uint32_t isunaligned : 1;
      uint32_t isrestrict : 1;
      uint32_t size : 6;
      uint32_t ismocom : 1;
      uint32_t islref : 1;
      uint32_t isrref : 1;
      uint32_t unused : 10;
    } s;
    uint32_t num;
  } attr;
};

struct pdb_array {
  uint16_t type;
  uint16_t index_type;
  unsigned int length;
};

struct pdb_arglist {
  unsigned int count;
  uint16_t args[1];
};

struct pdb_proc {
  uint16_t return_type;
  uint8_t calling_convention;
  uint8_t attributes;
  uint16_t num_args;
  uint16_t arg_list;
};

struct pdb_udt_src_line {
  uint16_t type;
  uint16_t source_file;
  uint32_t line;
};

struct pdb_type {
  struct pdb_type *next;
  uint16_t id;
  tree_node *tree;
  uint16_t cv_type;
  uint8_t data[1];
};

#define CV_BUILTIN_TYPE_VOID			0x0003
#define CV_BUILTIN_TYPE_SIGNED_CHARACTER	0x0010
#define CV_BUILTIN_TYPE_INT16SHORT		0x0011
#define CV_BUILTIN_TYPE_INT32LONG		0x0012
#define CV_BUILTIN_TYPE_UNSIGNED_CHARACTER	0x0020
#define CV_BUILTIN_TYPE_UINT16SHORT		0x0021
#define CV_BUILTIN_TYPE_UINT32LONG		0x0022
#define CV_BUILTIN_TYPE_SBYTE			0x0068
#define CV_BUILTIN_TYPE_BYTE			0x0069
#define CV_BUILTIN_TYPE_NARROW_CHARACTER	0x0070
#define CV_BUILTIN_TYPE_INT16			0x0072
#define CV_BUILTIN_TYPE_UINT16			0x0073
#define CV_BUILTIN_TYPE_INT32			0x0074
#define CV_BUILTIN_TYPE_UINT32			0x0075
#define CV_BUILTIN_TYPE_INT64			0x0076
#define CV_BUILTIN_TYPE_UINT64			0x0077

// from CV_prmode_e in cvdump
#define CV_TM_NPTR32			4
#define CV_TM_NPTR64			6

// from CV_call_e in cvdummp
#define CV_CALL_NEAR_C		0x00
#define CV_CALL_NEAR_FAST	0x04
#define CV_CALL_NEAR_STD	0x07
#define CV_CALL_THISCALL	0x0b

struct pdb_source_file {
  struct pdb_source_file *next;
  uint8_t hash[16];
  uint32_t str_offset;
  unsigned int num;
  char name[1];
};

#endif
