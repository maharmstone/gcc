#ifndef GCC_PDBOUT_H
#define GCC_PDBOUT_H 1

#define CODEVIEW_S_END		0x0006
#define CODEVIEW_S_GPROC32	0x1110
#define CODEVIEW_S_LPROC32	0x110f

#define CV_SIGNATURE_C13	4

struct pdb_func {
  struct pdb_func *next;
  char *name;
  int num;
  unsigned int public_flag;
};

#endif
