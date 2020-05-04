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

enum pdb_x86_register {
  CV_X86_NONE		= 0,
  CV_X86_AL		= 1,
  CV_X86_CL		= 2,
  CV_X86_DL		= 3,
  CV_X86_BL		= 4,
  CV_X86_AH		= 5,
  CV_X86_CH		= 6,
  CV_X86_DH		= 7,
  CV_X86_BH		= 8,
  CV_X86_AX		= 9,
  CV_X86_CX		= 10,
  CV_X86_DX		= 11,
  CV_X86_BX		= 12,
  CV_X86_SP		= 13,
  CV_X86_BP		= 14,
  CV_X86_SI		= 15,
  CV_X86_DI		= 16,
  CV_X86_EAX		= 17,
  CV_X86_ECX		= 18,
  CV_X86_EDX		= 19,
  CV_X86_EBX		= 20,
  CV_X86_ESP		= 21,
  CV_X86_EBP		= 22,
  CV_X86_ESI		= 23,
  CV_X86_EDI		= 24,
  CV_X86_ES		= 25,
  CV_X86_CS		= 26,
  CV_X86_SS		= 27,
  CV_X86_DS		= 28,
  CV_X86_FS		= 29,
  CV_X86_GS		= 30,
  CV_X86_IP		= 31,
  CV_X86_FLAGS		= 32,
  CV_X86_EIP		= 33,
  CV_X86_EFLAGS		= 34,
  CV_X86_CR0		= 80,
  CV_X86_CR1		= 81,
  CV_X86_CR2		= 82,
  CV_X86_CR3		= 83,
  CV_X86_CR4		= 84,
  CV_X86_DR0		= 90,
  CV_X86_DR1		= 91,
  CV_X86_DR2		= 92,
  CV_X86_DR3		= 93,
  CV_X86_DR4		= 94,
  CV_X86_DR5		= 95,
  CV_X86_DR6		= 96,
  CV_X86_DR7		= 97,
  CV_X86_GDTR		= 110,
  CV_X86_GDTL		= 111,
  CV_X86_IDTR		= 112,
  CV_X86_IDTL		= 113,
  CV_X86_LDTR		= 114,
  CV_X86_TR		= 115,
  CV_X86_ST0		= 128,
  CV_X86_ST1		= 129,
  CV_X86_ST2		= 130,
  CV_X86_ST3		= 131,
  CV_X86_ST4		= 132,
  CV_X86_ST5		= 133,
  CV_X86_ST6		= 134,
  CV_X86_ST7		= 135,
  CV_X86_CTRL		= 136,
  CV_X86_STAT		= 137,
  CV_X86_TAG		= 138,
  CV_X86_FPIP		= 139,
  CV_X86_FPCS		= 140,
  CV_X86_FPDO		= 141,
  CV_X86_FPDS		= 142,
  CV_X86_ISEM		= 143,
  CV_X86_FPEIP		= 144,
  CV_X86_FPEDO		= 145,
  CV_X86_MM0		= 146,
  CV_X86_MM1		= 147,
  CV_X86_MM2		= 148,
  CV_X86_MM3		= 149,
  CV_X86_MM4		= 150,
  CV_X86_MM5		= 151,
  CV_X86_MM6		= 152,
  CV_X86_MM7		= 153,
  CV_X86_XMM0		= 154,
  CV_X86_XMM1		= 155,
  CV_X86_XMM2		= 156,
  CV_X86_XMM3		= 157,
  CV_X86_XMM4		= 158,
  CV_X86_XMM5		= 159,
  CV_X86_XMM6		= 160,
  CV_X86_XMM7		= 161,
  CV_X86_XMM00		= 162,
  CV_X86_XMM01		= 163,
  CV_X86_XMM02		= 164,
  CV_X86_XMM03		= 165,
  CV_X86_XMM10		= 166,
  CV_X86_XMM11		= 167,
  CV_X86_XMM12		= 168,
  CV_X86_XMM13		= 169,
  CV_X86_XMM20		= 170,
  CV_X86_XMM21		= 171,
  CV_X86_XMM22		= 172,
  CV_X86_XMM23		= 173,
  CV_X86_XMM30		= 174,
  CV_X86_XMM31		= 175,
  CV_X86_XMM32		= 176,
  CV_X86_XMM33		= 177,
  CV_X86_XMM40		= 178,
  CV_X86_XMM41		= 179,
  CV_X86_XMM42		= 180,
  CV_X86_XMM43		= 181,
  CV_X86_XMM50		= 182,
  CV_X86_XMM51		= 183,
  CV_X86_XMM52		= 184,
  CV_X86_XMM53		= 185,
  CV_X86_XMM60		= 186,
  CV_X86_XMM61		= 187,
  CV_X86_XMM62		= 188,
  CV_X86_XMM63		= 189,
  CV_X86_XMM70		= 190,
  CV_X86_XMM71		= 191,
  CV_X86_XMM72		= 192,
  CV_X86_XMM73		= 193,
  CV_X86_XMM0L		= 194,
  CV_X86_XMM1L		= 195,
  CV_X86_XMM2L		= 196,
  CV_X86_XMM3L		= 197,
  CV_X86_XMM4L		= 198,
  CV_X86_XMM5L		= 199,
  CV_X86_XMM6L		= 200,
  CV_X86_XMM7L		= 201,
  CV_X86_XMM0H		= 202,
  CV_X86_XMM1H		= 203,
  CV_X86_XMM2H		= 204,
  CV_X86_XMM3H		= 205,
  CV_X86_XMM4H		= 206,
  CV_X86_XMM5H		= 207,
  CV_X86_XMM6H		= 208,
  CV_X86_XMM7H		= 209,
  CV_X86_MXCSR		= 211,
  CV_X86_EMM0L		= 220,
  CV_X86_EMM1L		= 221,
  CV_X86_EMM2L		= 222,
  CV_X86_EMM3L		= 223,
  CV_X86_EMM4L		= 224,
  CV_X86_EMM5L		= 225,
  CV_X86_EMM6L		= 226,
  CV_X86_EMM7L		= 227,
  CV_X86_EMM0H		= 228,
  CV_X86_EMM1H		= 229,
  CV_X86_EMM2H		= 230,
  CV_X86_EMM3H		= 231,
  CV_X86_EMM4H		= 232,
  CV_X86_EMM5H		= 233,
  CV_X86_EMM6H		= 234,
  CV_X86_EMM7H		= 235,
  CV_X86_MM00		= 236,
  CV_X86_MM01		= 237,
  CV_X86_MM10		= 238,
  CV_X86_MM11		= 239,
  CV_X86_MM20		= 240,
  CV_X86_MM21		= 241,
  CV_X86_MM30		= 242,
  CV_X86_MM31		= 243,
  CV_X86_MM40		= 244,
  CV_X86_MM41		= 245,
  CV_X86_MM50		= 246,
  CV_X86_MM51		= 247,
  CV_X86_MM60		= 248,
  CV_X86_MM61		= 249,
  CV_X86_MM70		= 250,
  CV_X86_MM71		= 251,
  CV_X86_YMM0		= 252,
  CV_X86_YMM1		= 253,
  CV_X86_YMM2		= 254,
  CV_X86_YMM3		= 255,
  CV_X86_YMM4		= 256,
  CV_X86_YMM5		= 257,
  CV_X86_YMM6		= 258,
  CV_X86_YMM7		= 259,
  CV_X86_YMM0H		= 260,
  CV_X86_YMM1H		= 261,
  CV_X86_YMM2H		= 262,
  CV_X86_YMM3H		= 263,
  CV_X86_YMM4H		= 264,
  CV_X86_YMM5H		= 265,
  CV_X86_YMM6H		= 266,
  CV_X86_YMM7H		= 267,
  CV_X86_YMM0I0		= 268,
  CV_X86_YMM0I1		= 269,
  CV_X86_YMM0I2		= 270,
  CV_X86_YMM0I3		= 271,
  CV_X86_YMM1I0		= 272,
  CV_X86_YMM1I1		= 273,
  CV_X86_YMM1I2		= 274,
  CV_X86_YMM1I3		= 275,
  CV_X86_YMM2I0		= 276,
  CV_X86_YMM2I1		= 277,
  CV_X86_YMM2I2		= 278,
  CV_X86_YMM2I3		= 279,
  CV_X86_YMM3I0		= 280,
  CV_X86_YMM3I1		= 281,
  CV_X86_YMM3I2		= 282,
  CV_X86_YMM3I3		= 283,
  CV_X86_YMM4I0		= 284,
  CV_X86_YMM4I1		= 285,
  CV_X86_YMM4I2		= 286,
  CV_X86_YMM4I3		= 287,
  CV_X86_YMM5I0		= 288,
  CV_X86_YMM5I1		= 289,
  CV_X86_YMM5I2		= 290,
  CV_X86_YMM5I3		= 291,
  CV_X86_YMM6I0		= 292,
  CV_X86_YMM6I1		= 293,
  CV_X86_YMM6I2		= 294,
  CV_X86_YMM6I3		= 295,
  CV_X86_YMM7I0		= 296,
  CV_X86_YMM7I1		= 297,
  CV_X86_YMM7I2		= 298,
  CV_X86_YMM7I3		= 299,
  CV_X86_YMM0F0		= 300,
  CV_X86_YMM0F1		= 301,
  CV_X86_YMM0F2		= 302,
  CV_X86_YMM0F3		= 303,
  CV_X86_YMM0F4		= 304,
  CV_X86_YMM0F5		= 305,
  CV_X86_YMM0F6		= 306,
  CV_X86_YMM0F7		= 307,
  CV_X86_YMM1F0		= 308,
  CV_X86_YMM1F1		= 309,
  CV_X86_YMM1F2		= 310,
  CV_X86_YMM1F3		= 311,
  CV_X86_YMM1F4		= 312,
  CV_X86_YMM1F5		= 313,
  CV_X86_YMM1F6		= 314,
  CV_X86_YMM1F7		= 315,
  CV_X86_YMM2F0		= 316,
  CV_X86_YMM2F1		= 317,
  CV_X86_YMM2F2		= 318,
  CV_X86_YMM2F3		= 319,
  CV_X86_YMM2F4		= 320,
  CV_X86_YMM2F5		= 321,
  CV_X86_YMM2F6		= 322,
  CV_X86_YMM2F7		= 323,
  CV_X86_YMM3F0		= 324,
  CV_X86_YMM3F1		= 325,
  CV_X86_YMM3F2		= 326,
  CV_X86_YMM3F3		= 327,
  CV_X86_YMM3F4		= 328,
  CV_X86_YMM3F5		= 329,
  CV_X86_YMM3F6		= 330,
  CV_X86_YMM3F7		= 331,
  CV_X86_YMM4F0		= 332,
  CV_X86_YMM4F1		= 333,
  CV_X86_YMM4F2		= 334,
  CV_X86_YMM4F3		= 335,
  CV_X86_YMM4F4		= 336,
  CV_X86_YMM4F5		= 337,
  CV_X86_YMM4F6		= 338,
  CV_X86_YMM4F7		= 339,
  CV_X86_YMM5F0		= 340,
  CV_X86_YMM5F1		= 341,
  CV_X86_YMM5F2		= 342,
  CV_X86_YMM5F3		= 343,
  CV_X86_YMM5F4		= 344,
  CV_X86_YMM5F5		= 345,
  CV_X86_YMM5F6		= 346,
  CV_X86_YMM5F7		= 347,
  CV_X86_YMM6F0		= 348,
  CV_X86_YMM6F1		= 349,
  CV_X86_YMM6F2		= 350,
  CV_X86_YMM6F3		= 351,
  CV_X86_YMM6F4		= 352,
  CV_X86_YMM6F5		= 353,
  CV_X86_YMM6F6		= 354,
  CV_X86_YMM6F7		= 355,
  CV_X86_YMM7F0		= 356,
  CV_X86_YMM7F1		= 357,
  CV_X86_YMM7F2		= 358,
  CV_X86_YMM7F3		= 359,
  CV_X86_YMM7F4		= 360,
  CV_X86_YMM7F5		= 361,
  CV_X86_YMM7F6		= 362,
  CV_X86_YMM7F7		= 363,
  CV_X86_YMM0D0		= 364,
  CV_X86_YMM0D1		= 365,
  CV_X86_YMM0D2		= 366,
  CV_X86_YMM0D3		= 367,
  CV_X86_YMM1D0		= 368,
  CV_X86_YMM1D1		= 369,
  CV_X86_YMM1D2		= 370,
  CV_X86_YMM1D3		= 371,
  CV_X86_YMM2D0		= 372,
  CV_X86_YMM2D1		= 373,
  CV_X86_YMM2D2		= 374,
  CV_X86_YMM2D3		= 375,
  CV_X86_YMM3D0		= 376,
  CV_X86_YMM3D1		= 377,
  CV_X86_YMM3D2		= 378,
  CV_X86_YMM3D3		= 379,
  CV_X86_YMM4D0		= 380,
  CV_X86_YMM4D1		= 381,
  CV_X86_YMM4D2		= 382,
  CV_X86_YMM4D3		= 383,
  CV_X86_YMM5D0		= 384,
  CV_X86_YMM5D1		= 385,
  CV_X86_YMM5D2		= 386,
  CV_X86_YMM5D3		= 387,
  CV_X86_YMM6D0		= 388,
  CV_X86_YMM6D1		= 389,
  CV_X86_YMM6D2		= 390,
  CV_X86_YMM6D3		= 391,
  CV_X86_YMM7D0		= 392,
  CV_X86_YMM7D1		= 393,
  CV_X86_YMM7D2		= 394,
  CV_X86_YMM7D3		= 395,
};

#endif
