#ifndef GCC_PDBOUT_H
#define GCC_PDBOUT_H 1

#define CODEVIEW_S_END		0x0006
#define CODEVIEW_S_LDATA32	0x110c
#define CODEVIEW_S_GDATA32	0x110d
#define CODEVIEW_S_LPROC32	0x110f
#define CODEVIEW_S_GPROC32	0x1110
#define CODEVIEW_LF_FIELDLIST	0x1203
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
  char* name;
};

struct pdb_fieldlist {
  unsigned int count;
  struct pdb_fieldlist_entry *entries;
};

struct pdb_type {
  struct pdb_type *next;
  uint16_t id;
  tree_node *tree;
  uint16_t cv_type;
  uint8_t data[1];
};

#endif
