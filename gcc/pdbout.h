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

struct pdb_struct_fieldlist_entry {
  uint32_t offset;
  uint16_t type;
  char *name;
};

struct pdb_struct_fieldlist {
  uint16_t num;
  unsigned int count;
  struct pdb_struct_fieldlist_entry entries[1];
};

struct pdb_fieldlist_entry {
  uint16_t cv_type;
  uint16_t type;
  uint16_t offset;
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
