#ifndef GCC_PDBOUT_H
#define GCC_PDBOUT_H 1

#define CODEVIEW_S_END		0x0006
#define CODEVIEW_LF_POINTER	0x1002
#define CODEVIEW_S_LDATA32	0x110c
#define CODEVIEW_S_GDATA32	0x110d
#define CODEVIEW_S_LPROC32	0x110f
#define CODEVIEW_S_GPROC32	0x1110
#define CODEVIEW_LF_FIELDLIST	0x1203
#define CODEVIEW_LF_ENUMERATE	0x1502
#define CODEVIEW_LF_CLASS	0x1504
#define CODEVIEW_LF_STRUCTURE	0x1505
#define CODEVIEW_LF_UNION	0x1506
#define CODEVIEW_LF_ENUM	0x1507
#define CODEVIEW_LF_MEMBER	0x150d

#define CV_SIGNATURE_C13	4

struct pdb_func {
  struct pdb_func *next;
  char *name;
  int num;
  unsigned int public_flag;
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

struct pdb_type {
  struct pdb_type *next;
  uint16_t id;
  tree_node *tree;
  uint16_t cv_type;
  uint8_t data[1];
};

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

#endif
