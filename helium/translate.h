#ifndef TRANSLATE_H_ZDIHRPIG
#define TRANSLATE_H_ZDIHRPIG

#include "ast.h"
#include "temp.h"
#include "frame.h"
#include "types.h"
#include "util.h"
#include "program.h"

// forward declaration
struct Semant_ContextType;

typedef struct Tr_exp_ * Tr_exp;
typedef struct Tr_level_ * Tr_level;
typedef struct Tr_access_ * Tr_access;

typedef struct Tr_accessList_ * Tr_accessList;
struct Tr_accessList_
{
    Tr_access head;
    Tr_accessList tail;
};

typedef struct Tr_expList_ * Tr_expList;
struct Tr_expList_
{
    Tr_exp head;
    Tr_expList tail;
};

Tr_expList Tr_ExpList (Tr_exp head, Tr_expList tail);

void Tr_Init (struct Semant_ContextType * c);
void Tr_ProcEntryExit (struct Semant_ContextType * c, Tr_level level, Tr_exp body);

/***************
 *  Variables  *
 ***************/

Tr_exp Tr_SimpleVar (Tr_access access, Tr_level level);
Tr_exp Tr_FieldVar (Tr_exp var, Ty_ty type, S_symbol field, bool deref);
Tr_exp Tr_SubscriptVar (Tr_exp var, Ty_ty type, Tr_exp subscript, bool deref);

/*****************
 *  Expressions  *
 *****************/

Tr_exp Tr_Seq (Tr_exp seq, Tr_exp current);
Tr_exp Tr_Call (Temp_label label, Tr_level encolosing, Tr_level own, Tr_expList args);
Tr_exp Tr_Nil (void);
Tr_exp Tr_Void (void);
Tr_exp Tr_Int (int value);
Tr_exp Tr_String (struct Semant_ContextType * c, const char * value);
Tr_exp Tr_Op (A_oper op, Tr_exp left, Tr_exp right, Ty_ty ty);
Tr_exp Tr_ArrayExp (Tr_access access, Ty_ty type, Tr_expList list, int offset);
Tr_exp Tr_RecordExp (Tr_access access, Ty_ty type, Tr_expList list, int offset);
Tr_exp Tr_Assign (Tr_exp var, Tr_exp value);
Tr_exp Tr_If (Tr_exp test, Tr_exp te, Tr_exp fe);
Tr_exp Tr_While (Tr_exp test, Tr_exp body, Temp_label done);
Tr_exp Tr_For (Tr_exp lo, Tr_exp hi, Tr_exp body, Temp_label done);
Tr_exp Tr_Break (Temp_label done);
Tr_exp Tr_Ret (Tr_exp exp);
Tr_exp Tr_Exit (Tr_exp exp);
Tr_exp Tr_Asm (const char * code, Tr_exp data, U_stringList dst, U_stringList src);

/**************
 *  Whatever  *
 **************/

Tr_level Tr_NewLevel (Tr_level parent, Temp_label name, U_boolList formals);

Temp_label Tr_ScopedLabel (Tr_level level, const char * name);

Tr_accessList Tr_Formals (Tr_level level);

Tr_access Tr_AllocVirtual (Tr_level level);
Tr_access Tr_AllocMaterialize (Tr_access access, Tr_level level, Ty_ty type, bool escape);
Tr_access Tr_Alloc (Tr_level level, Ty_ty type, bool escape);

#endif /* end of include guard: TRANSLATE_H_ZDIHRPIG */
