/*
 *  Ecmascript bytecode
 */

#ifndef DUK_JS_BYTECODE_H_INCLUDED
#define DUK_JS_BYTECODE_H_INCLUDED

/*
 *  Logical instruction layout
 *  ==========================
 *
 *  !3!3!2!2!2!2!2!2!2!2!2!2!1!1!1!1!1!1!1!1!1!1! ! ! ! ! ! ! ! ! ! !
 *  !1!0!9!8!7!6!5!4!3!2!1!0!9!8!7!6!5!4!3!2!1!0!9!8!7!6!5!4!3!2!1!0!
 *  +---------------------------------------------------+-----------+
 *  !       C         !       B         !      A        !    OP     !
 *  +---------------------------------------------------+-----------+
 *
 *  OP (6 bits):  opcode (DUK_OP_*), access should be fastest
 *  A (8 bits):   typically a target register number
 *  B (9 bits):   typically first source register/constant number
 *  C (9 bits):   typically second source register/constant number
 *
 *  Some instructions combine BC or ABC together for larger parameter values.
 *  Signed integers (e.g. jump offsets) are encoded as unsigned, with an opcode
 *  specific bias.  B and C may denote a register or a constant, see
 *  DUK_BC_ISREG() and DUK_BC_ISCONST().
 *
 *  Note: macro naming is a bit misleading, e.g. "ABC" in macro name but
 *  the field layout is logically "CBA".
 */ 

typedef duk_u32 duk_instr;

#define  DUK_DEC_OP(x)               ((x) & 0x3f)
#define  DUK_DEC_A(x)                (((x) >> 6) & 0xff)
#define  DUK_DEC_B(x)                (((x) >> 14) & 0x1ff)
#define  DUK_DEC_C(x)                (((x) >> 23) & 0x1ff)
#define  DUK_DEC_BC(x)               (((x) >> 14) & 0x3ffff)
#define  DUK_DEC_ABC(x)              (((x) >> 6) & 0x3ffffff)

#define  DUK_ENC_OP_ABC(op,abc)      ((duk_instr) (((abc) << 6) | (op)))
#define  DUK_ENC_OP_A_BC(op,a,bc)    ((duk_instr) (((bc) << 14) | ((a) << 6) | (op)))
#define  DUK_ENC_OP_A_B_C(op,a,b,c)  ((duk_instr) (((c) << 23) | ((b) << 14) | ((a) << 6) | (op)))
#define  DUK_ENC_OP_A_B(op,a,b)      DUK_ENC_OP_A_B_C(op,a,b,0)
#define  DUK_ENC_OP_A(op,a)          DUK_ENC_OP_A_B_C(op,a,0,0)

#define  DUK_BC_OP_MIN               0
#define  DUK_BC_OP_MAX               0x3f
#define  DUK_BC_A_MIN                0
#define  DUK_BC_A_MAX                0xff
#define  DUK_BC_B_MIN                0
#define  DUK_BC_B_MAX                0x1ff
#define  DUK_BC_C_MIN                0
#define  DUK_BC_C_MAX                0x1ff
#define  DUK_BC_BC_MIN               0
#define  DUK_BC_BC_MAX               0x3ffff
#define  DUK_BC_ABC_MIN              0
#define  DUK_BC_ABC_MAX              0x3ffffff
#define  DUK_BC_EXTRAOP_MIN          DUK_BC_A_MIN
#define  DUK_BC_EXTRAOP_MAX          DUK_BC_A_MAX

#define  DUK_OP_LDREG                0 
#define  DUK_OP_STREG                1  /* FIXME: UNUSED */
#define  DUK_OP_LDCONST              2
#define  DUK_OP_LDINT                3
#define  DUK_OP_LDINTX               4  /* FIXME: UNUSED */
#define  DUK_OP_MPUTOBJ              5
#define  DUK_OP_MPUTARR              6
#define  DUK_OP_NEW                  7
#define  DUK_OP_REGEXP               8
#define  DUK_OP_CSREG                9
#define  DUK_OP_GETVAR               10
#define  DUK_OP_PUTVAR               11
#define  DUK_OP_DECLVAR              12
#define  DUK_OP_DELVAR               13
#define  DUK_OP_CSVAR                14
#define  DUK_OP_CLOSURE              15
#define  DUK_OP_GETPROP              16
#define  DUK_OP_PUTPROP              17
#define  DUK_OP_DELPROP              18
#define  DUK_OP_CSPROP               19
#define  DUK_OP_ADD                  20
#define  DUK_OP_SUB                  21
#define  DUK_OP_MUL                  22
#define  DUK_OP_DIV                  23
#define  DUK_OP_MOD                  24
#define  DUK_OP_UNM                  25
#define  DUK_OP_UNP                  26
#define  DUK_OP_INC                  27
#define  DUK_OP_DEC                  28
#define  DUK_OP_BAND                 29
#define  DUK_OP_BOR                  30
#define  DUK_OP_BXOR                 31
#define  DUK_OP_BASL                 32
#define  DUK_OP_BLSR                 33
#define  DUK_OP_BASR                 34
#define  DUK_OP_BNOT                 35
#define  DUK_OP_LNOT                 36
#define  DUK_OP_EQ                   37
#define  DUK_OP_NEQ                  38
#define  DUK_OP_SEQ                  39
#define  DUK_OP_SNEQ                 40
#define  DUK_OP_GT                   41
#define  DUK_OP_GE                   42
#define  DUK_OP_LT                   43
#define  DUK_OP_LE                   44
#define  DUK_OP_IF                   45
#define  DUK_OP_INSTOF               46
#define  DUK_OP_IN                   47
#define  DUK_OP_JUMP                 48
#define  DUK_OP_RETURN               49
#define  DUK_OP_CALL                 50
#define  DUK_OP_LABEL                51
#define  DUK_OP_ENDLABEL             52
#define  DUK_OP_BREAK                53
#define  DUK_OP_CONTINUE             54
#define  DUK_OP_TRYCATCH             55
#define  DUK_OP_56                   56
#define  DUK_OP_57                   57
#define  DUK_OP_58                   58
#define  DUK_OP_59                   59
#define  DUK_OP_60                   60
#define  DUK_OP_EXTRA                61
#define  DUK_OP_DEBUG                62
#define  DUK_OP_INVALID              63

/* DUK_OP_EXTRA, sub-operation in A */
#define  DUK_EXTRAOP_NOP             0
#define  DUK_EXTRAOP_LDTHIS          1
#define  DUK_EXTRAOP_LDUNDEF         2
#define  DUK_EXTRAOP_LDNULL          3
#define  DUK_EXTRAOP_LDTRUE          4
#define  DUK_EXTRAOP_LDFALSE         5
#define  DUK_EXTRAOP_NEWOBJ          6
#define  DUK_EXTRAOP_NEWARR          7
#define  DUK_EXTRAOP_SETALEN         8
#define  DUK_EXTRAOP_TYPEOF          9
#define  DUK_EXTRAOP_TYPEOFID        10
#define  DUK_EXTRAOP_TONUM           11
#define  DUK_EXTRAOP_INITENUM        12
#define  DUK_EXTRAOP_NEXTENUM        13
#define  DUK_EXTRAOP_INITSET         14
#define  DUK_EXTRAOP_INITGET         15
#define  DUK_EXTRAOP_ENDTRY          16
#define  DUK_EXTRAOP_ENDCATCH        17
#define  DUK_EXTRAOP_ENDFIN          18
#define  DUK_EXTRAOP_THROW           19
#define  DUK_EXTRAOP_INVLHS          20

/* DUK_OP_DEBUG, sub-operation in A */
#define  DUK_DEBUGOP_DUMPREG         0
#define  DUK_DEBUGOP_DUMPREGS        1
#define  DUK_DEBUGOP_DUMPTHREAD      2
#define  DUK_DEBUGOP_LOGMARK         3

/* DUK_OP_CALL flags in A */
#define  DUK_BC_CALL_FLAG_TAILCALL           (1 << 0)
#define  DUK_BC_CALL_FLAG_EVALCALL           (1 << 1)

/* DUK_OP_TRYCATCH flags in A */
#define  DUK_BC_TRYCATCH_FLAG_HAVE_CATCH     (1 << 0)
#define  DUK_BC_TRYCATCH_FLAG_HAVE_FINALLY   (1 << 1)
#define  DUK_BC_TRYCATCH_FLAG_CATCH_BINDING  (1 << 2)
#define  DUK_BC_TRYCATCH_FLAG_WITH_BINDING   (1 << 3)

/* DUK_OP_RETURN flags in A */
#define  DUK_BC_RETURN_FLAG_FAST             (1 << 0)
#define  DUK_BC_RETURN_FLAG_HAVE_RETVAL      (1 << 1)

/* DUK_OP_DECLVAR flags in A; bottom bits are reserved for propdesc flags (DUK_PROPDESC_FLAG_XXX) */
#define  DUK_BC_DECLVAR_FLAG_UNDEF_VALUE     (1 << 4)  /* use 'undefined' for value automatically */
#define  DUK_BC_DECLVAR_FLAG_FUNC_DECL       (1 << 5)  /* function declaration */

/* misc constants and helper macros */
#define  DUK_BC_REGLIMIT             256  /* if B/C is >= this value, refers to a const */
#define  DUK_BC_ISREG(x)             ((x) < DUK_BC_REGLIMIT)
#define  DUK_BC_ISCONST(x)           ((x) >= DUK_BC_REGLIMIT)
#define  DUK_BC_LDINT_BIAS           (1 << 17)
#define  DUK_BC_LDINTX_SHIFT         18
#define  DUK_BC_JUMP_BIAS            (1 << 25)

#endif  /* DUK_JS_BYTECODE_H_INCLUDED */

