#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include "ext/table.h"
#include "ext/list.h"

#include "semant.h"
#include "ast.h"
#include "types.h"
#include "symbol.h"
#include "escape.h"
#include "error.h"
#include "env.h"

#define ERROR(loc, code, format, ...)                                    \
    {                                                                    \
        Vector_PushBack(&context->module->errors.semant,                 \
                Error_New(loc, code, format, __VA_ARGS__));              \
    }                                                                    \

#define ERROR_UNEXPECTED_TYPE(loc, expected, actual)                     \
    ERROR(                                                               \
        loc,                                                             \
        3001,                                                            \
        "Expected '%s', got '%s'",                                       \
        expected->meta.name,                                             \
        actual->meta.name);                                              \

#define ERROR_MALFORMED_EXP(loc, text)                                   \
    ERROR(                                                               \
        loc,                                                             \
        3002,                                                            \
        "Malformed exprassion: %s",                                      \
        text);                                                           \

#define ERROR_INVALID_TYPE(loc, name)                                    \
    ERROR(                                                               \
        loc,                                                             \
        3003,                                                            \
        "Invalid type '%s'",                                             \
        S_Name(name));                                                   \

#define ERROR_UNKNOWN_TYPE(loc, name)                                    \
    ERROR(                                                               \
        loc,                                                             \
        3004,                                                            \
        "Unknown type '%s'",                                             \
        S_Name(name));                                                   \

#define ERROR_UNKNOWN_SYMBOL(loc, sym)                                   \
    ERROR(                                                               \
        loc,                                                             \
        3005,                                                            \
        "Unknown symbol '%s'",                                           \
        S_Name(sym));                                                    \

#define ERROR_INVALID_EXPRESSION(loc)                                    \
    ERROR(                                                               \
        loc,                                                             \
        3009,                                                            \
        "Invalid expression", "");

#define ERROR_EXPECTED_CONSTANT_EXPRESSION(loc)                          \
    ERROR(                                                               \
        loc,                                                             \
        3011,                                                            \
        "Expected constat expression", "");

typedef struct Semant_ExpType
{
    Tr_exp exp;
    Ty_ty ty;

} Semant_Exp;

static bool is_auto (Ty_ty ty)
{
    return ty == Ty_Auto();
}

static bool is_invalid (Ty_ty ty)
{
    return ty == Ty_Invalid();
}

static bool is_int_type (Ty_ty ty)
{
    return ty == Ty_Int();
}

static bool is_int (Semant_Exp exp)
{
    return exp.ty == Ty_Int();
}

static bool is_string (Semant_Exp exp)
{
    return exp.ty == Ty_String();
}

static bool same_type (Semant_Exp a, Semant_Exp b)
{
    return a.ty == b.ty;
}

static Semant_Exp Expression_New (Tr_exp exp, Ty_ty ty)
{
    Semant_Exp r;
    r.exp = exp;
    r.ty = ty;
    return r;
}

static Ty_ty GetActualType (Ty_ty ty)
{
    if (ty->kind == Ty_name)
    {
        if (ty->u.name.ty == NULL)
        {
            printf ("NULL");
        }
        return ty->u.name.ty;
    }
    return ty;
}

static Semant_Exp TransExp (Semant_Context context, A_exp exp);
static Tr_exp TransDec (Semant_Context context, A_dec dec);

static Semant_Exp TransScope (Semant_Context context, A_scope scope)
{
    Semant_Exp r = { Tr_Void(), Ty_Void() };
    Tr_exp seq = NULL;
    LIST_FOREACH (stm, scope->list)
    {
        switch (stm->kind)
        {
        case A_stmExp:
        {
            r = TransExp (context, stm->u.exp);
            break;
        }
        case A_stmDec:
        {
            r.exp = TransDec (context, stm->u.dec);
            r.ty = Ty_Void();
            break;
        }
        }

        seq = Tr_Seq (seq, r.exp);
    }

    return Expression_New (seq, r.ty);
}

// TODO hash function to uniquly identify a type
static Ty_ty TransTyp (Semant_Context context, A_ty ty)
{
    assert (ty);

    switch (ty->kind)
    {
    case A_nameTy:
    {
        Ty_ty type = (Ty_ty)S_Look (context->tenv, ty->u.name);
        if (!type)
        {
            ERROR_UNKNOWN_TYPE (&ty->loc, ty->u.name);
            return Ty_Invalid();
        }

        return type;
    }
    case A_arrayTy:
    {
        struct A_arrayTy_t arrayTy = ty->u.array;

        /*
         * If the type of array is not a valid type we change it to int and proceed, this will
         * not bring ripple effect to error generation. The user will be only notified by the
         * error produced by A_nameTy look-up
         */
        Ty_ty type = TransTyp (context, arrayTy.type);
        if (is_invalid (type))
        {
            type = Ty_Int();
        }

        /*
         * As with the type the type we do not break the translation process and replace an invalid
         * size expression with int(1), spawn error and proceed.
         */
        int size;
        if (arrayTy.size->kind != A_intExp)
        {
            ERROR_EXPECTED_CONSTANT_EXPRESSION (&arrayTy.size->loc)
            size = 1;
        }
        else
        {
            size = arrayTy.size->u.intt;
        }

        // TODO find a way to uniquly  identify the anonymous array type and return a very first
        // parse type instance so the type checks would be correct
        return Ty_Array (type, size);
    }
    case A_recordTy:
    {
        Ty_fieldList flist = NULL;
        LIST_FOREACH (field, ty->u.record)
        {
            /*
             * Same as for the array translation we do not break the process but simply fallback
             * to the default type int
             */
            Ty_ty type = TransTyp (context, field->type);
            if (is_invalid (type))
            {
                type = Ty_Int();
            }

            LIST_PUSH (flist, Ty_Field (field->name, type))
        }

        // TODO find a way to uniquly  identify the anonymous record type and return a very first
        // parse type instance so the type checks would be correct
        return Ty_Record (flist);
    }
    default:
    {
        assert (0);
    }
    }
}

// TODO you need to do it in two passes: 1 - names, 2 - definitions
static Tr_exp TransDec (Semant_Context context, A_dec dec)
{
    A_loc loc = &dec->loc;

    switch (dec->kind)
    {
    // TODO when forward declaration will be available check for cycle defines
    case A_typeDec:
    {
        struct A_decType_t decType = dec->u.type;
        Ty_ty type = TransTyp (context, decType.type);
        // fallback to int if type is invalid
        if (is_invalid (type))
        {
            type = Ty_Int();
        }
        Ty_ty def = Ty_Name (decType.name, type);
        S_Enter (context->tenv, decType.name, def);;
        return Tr_Void();
    }
    case A_varDec:
    {
        struct A_decVar_t decVar = dec->u.var;

        Ty_ty dty = NULL;
        if (decVar.type)
        {
            dty = TransTyp (context, decVar.type);
            /*
             * The type error was spawn by TransTyp already, this fallback will require type
             * inferring based on init expression if it exists, otherwise the translation will
             * return Tr_Void, in this case the variable won't be declared and all reference to
             * it will be illegal.
             */
            if (is_invalid (dty))
            {
                dty = NULL;
            }
            else
            {
                dty = GetActualType (dty);
            }
        }

        Ty_ty ity = NULL;
        Tr_exp iexp = NULL;
        if (decVar.init)
        {
            Semant_Exp sexp = TransExp (context, decVar.init);
            /*
             * If the initialization expression is invalid we simply drop it, the actual error was
             * spawn by TransExp scope.
             */
            if (!is_invalid (sexp.ty))
            {
                iexp = sexp.exp;
                ity = sexp.ty;
            }
        }

        Tr_access access = NULL;
        if (iexp)
        {
            access = Tr_Alloc (context->level, Ty_Int(), decVar.escape);
            iexp = Tr_Assign (Tr_SimpleVar (access, context->level), iexp);

            /*
             * If provided initialization expression is not of the variable type, spawn an error
             * and drop initialization completely.
             */
            if (dty && dty != ity)
            {
                ERROR_UNEXPECTED_TYPE (loc, dty, ity);
                iexp = Tr_Void();
            }
            /*
             * Type inferring
             */
            else
            {
                dty = ity;
            }

        }
        /*
         * We have only type supplied so no initialization is made.
         */
        else if (dty)
        {
            access = Tr_Alloc (context->level, GetActualType (dty), decVar.escape);
            iexp = Tr_Void();
        }
        else
        {
            return Tr_Void();
        }

        S_Enter (context->venv, decVar.var, Env_VarEntryNew (access, dty));

        return iexp;
    }
    // TODO functions without any exp?
    // TODO general and main fn
    case A_functionDec:
    {
        struct A_decFn_t decFn = dec->u.function;

        // translate name
        Temp_label label = Tr_ScopedLabel (context->level, decFn.name->name);
        bool is_main = strcmp (label->name, "main") == 0;

        // translate return type
        Ty_ty rty = NULL;
        if (decFn.type)
        {
            rty =  TransTyp (context, decFn.type);
            if (is_main && !is_int_type (rty))
            {
                ERROR_UNEXPECTED_TYPE (loc, Ty_Int(), rty);
                // change to int and try parse the rest
                rty = Ty_Int();
            }
        }
        else if (is_main)
        {
            rty = Ty_Int();
        }
        else
        {
            rty = Ty_Auto();
        }

        // translate formal parameters
        S_symbolList names = NULL;
        Ty_tyList types = NULL;
        U_boolList escapes = NULL;
        LIST_FOREACH (f, decFn.params)
        {
            Ty_ty fty = TransTyp (context, f->type);

            LIST_PUSH (names, f->name);
            LIST_PUSH (types, fty);
            LIST_PUSH (escapes, f->escape);
        }

        // create new frame
        Tr_level level = Tr_NewLevel (context->level, label, escapes);

        // create environment function entry
        Env_Entry entry = Env_FunEntryNew (level, label, names, types, rty);
        S_Enter (context->venv, decFn.name, entry);

        // create new scope for the body
        S_BeginScope (context->venv);
        S_BeginScope (context->tenv);

        // add each formal parameter binding to the newly entered scope
        {
            A_fieldList f = decFn.params;
            Tr_accessList al = Tr_Formals (entry->u.fun.level);
            Ty_tyList t = types;
            for (; f; f = f->tail, t = t->tail, al = al->tail)
            {
                S_Enter (
                    context->venv,
                    f->head->name,
                    Env_VarEntryNew (al->head, t->head));
            }
        }

        // add implicit return
        A_stm last;
        LIST_BACK (decFn.scope->list, last);
        if (last->kind == A_stmExp && last->u.exp->kind != A_retExp)
        {
            A_exp e = last->u.exp;
            last->u.exp = A_RetExp (&e->loc, e);
        }
        /*
         * main function is treated specially because its job is to return program's execution
         * status, where 0 means success and any positive value is treated as an error. Thus if
         * the last statement in the main function is some sort of declaration we add implicit
         * ret 0 statement to the main's body end.
         */
        else if (last->kind == A_stmDec && is_main)
        {
            LIST_PUSH (decFn.scope->list, A_StmExp (A_RetExp (loc, A_IntExp (loc, 0))));
        }

        // save current frame before processing the body
        Tr_level current = context->level;

        // set the new frame as current so the body can use it
        context->level = level;

        // translate body
        Semant_Exp sexp = TransScope (context, decFn.scope);
        Tr_ProcEntryExit (context, context->level, sexp.exp);

        // restore frame
        context->level = current;

        // final type checks
        if (is_auto (rty))
        {
            entry->u.fun.result = sexp.ty;
        }
        else if (is_main && !is_int (sexp))
        {
            ERROR_UNEXPECTED_TYPE (&last->u.exp->loc, Ty_Int(), sexp.ty);
        }
        else if (!is_invalid (rty) && !is_invalid (rty) && rty != sexp.ty)
        {
            ERROR_UNEXPECTED_TYPE (&last->u.exp->loc, rty, sexp.ty);
        }

        // restore scope
        S_EndScope (context->venv);
        S_EndScope (context->tenv);

        return Tr_Void();
    }
    }
}

static Semant_Exp TransVar (Semant_Context context, A_var var)
{
    A_loc loc = &var->loc;
    Semant_Exp e_invalid = {Tr_Void(), Ty_Invalid()};

    /*
     * indicates nesting level, if level > 0 we just calculate correct offset, once we got back
     * to the original call(level==0) we read from the offset(base), e.g:
     *
     * Expresison:
     * foo.bar.x;
     *
     * Looks like this inside AST:
     * (((foo).bar).x)
     *
     * And parsed as:
     * $1 = foo    -> T_Temp(foo) # or anything that results as register
     * $2 = $1.bar -> T_Binop(T_plus, $1 + Offset(bar))
     * $3 = $1.x   -> T_Mem(T_Binop(T_plus, $2 + Offset(x)))
     */
    static int level = 0;

    switch (var->kind)
    {
    case A_simpleVar:
    {
        Env_Entry e = (Env_Entry)S_Look (context->venv, var->u.simple);
        if (!e)
        {
            ERROR_UNKNOWN_SYMBOL (&var->loc, var->u.simple)
            return e_invalid;
        }
        else if (e->kind == Env_varEntry)
        {
            return Expression_New (
                       Tr_SimpleVar (e->u.var.access, context->level),
                       GetActualType (e->u.var.ty));
        }
        else
        {
            ERROR_UNKNOWN_SYMBOL (loc, var->u.simple);
            return e_invalid;
        }
    }
    case A_fieldVar:
    {
        struct A_varField_t varField = var->u.field;

        level++;
        Semant_Exp vexp = TransVar (context, varField.var);
        level--;

        if (is_invalid (vexp.ty))
        {
            return e_invalid;
        }
        else if (vexp.ty->kind != Ty_record)
        {
            ERROR (
                &var->loc,
                3012,
                "Cannot access field '%s' of non record instance of type '%s'",
                varField.sym->name,
                vexp.ty->meta.name);

            return e_invalid;
        }

        LIST_FOREACH (f, vexp.ty->u.record)
        {
            if (f->name == varField.sym)
            {
                Ty_ty ty = GetActualType (f->ty);
                Tr_exp exp = Tr_FieldVar (vexp.exp, vexp.ty, f->name, level == 0);
                return Expression_New (exp, ty);
            }
        }

        ERROR (
            &var->loc,
            3012,
            "Record type '%s' does not contain field '%s'",
            vexp.ty->meta.name,
            varField.sym->name);

        return e_invalid;
    }
    case A_subscriptVar:
    {
        struct A_varSubscript_t varSubscript = var->u.subscript;

        level++;
        Semant_Exp vexp = TransVar (context, varSubscript.var);
        level--;

        if (is_invalid (vexp.ty))
        {
            return e_invalid;
        }
        else if (vexp.ty->kind != Ty_array)
        {
            ERROR (
                &var->loc,
                3012,
                "Cannot subscript non array instance of type '%s'",
                vexp.ty->meta.name);

            return e_invalid;
        }

        Semant_Exp sexp = TransExp (context, var->u.subscript.exp);
        /*
         * Essentially, array subscript expects an Int expression, if the subscript is not an Int
         * we simply spawn an error and try to recover by using a correct Int(0) translation.
         */
        if (!is_int (sexp))
        {
            ERROR_UNEXPECTED_TYPE (&varSubscript.exp->loc, Ty_Int(), sexp.ty);
            sexp.exp = Tr_Int (0);
            sexp.ty = Ty_Int();
        }

        Tr_exp exp = Tr_SubscriptVar (vexp.exp, vexp.ty, sexp.exp, level == 0);
        return Expression_New (exp, vexp.ty->u.array.type);
    }
    default:
    {
        assert (0);
    }
    }
}

/*
 * TODO
 * Returns default initialization for a type
 * TODO move to translate
 */
static Semant_Exp TransDefaultValue (Tr_access access, Ty_ty type, int offset)
{
    type = GetActualType (type);
    switch (type->kind)
    {
    case Ty_int:
    {
        return Expression_New (Tr_Int (0), Ty_Int());
    }
    case Ty_array:
    {
        int size = type->u.array.size;
        int typesize = Ty_SizeOf (type->u.array.type);
        Tr_expList el = NULL;
        int o = offset;
        while (size--)
        {
            LIST_PUSH (el, TransDefaultValue (access, type->u.array.type, o).exp);
            o += typesize;
        }

        return Expression_New (Tr_ArrayExp (access, type, el, offset), type);
    }
    case Ty_record:
    {
        int o = offset;
        Tr_expList el = NULL;
        LIST_FOREACH (f, type->u.record)
        {
            LIST_PUSH (el, TransDefaultValue (access, f->ty, o).exp);
            o += Ty_SizeOf (f->ty);
        }

        return Expression_New (Tr_RecordExp (access, type, el, offset), type);
    }
    default:
    {
        assert (0);
    }
    }
}

static Semant_Exp TransExp (Semant_Context context, A_exp exp)
{
    A_loc loc = &exp->loc;
    Semant_Exp e_invalid = {Tr_Void(), Ty_Invalid()};
    Semant_Exp e_void = {Tr_Void(), Ty_Void()};

    switch (exp->kind)
    {
    case A_seqExp:
    {
        A_expList l = exp->u.seq;
        if (!l)
        {
            return e_void;
        }
        Semant_Exp r;
        Tr_exp seq = NULL;
        LIST_FOREACH (e, l)
        {
            r = TransExp (context, e);
            seq = Tr_Seq (seq, r.exp);
        }
        return Expression_New (seq, r.ty);
    }
    case A_varExp:
    {
        return TransVar (context, exp->u.var);
    }
    case A_asmExp:
    {
        const char * data = exp->u.assembly.data;
        U_stringList dst = exp->u.assembly.dst;
        U_stringList src = exp->u.assembly.src;

        return Expression_New (
                   Tr_Asm (
                       exp->u.assembly.code,
                       data == NULL ? NULL : Tr_String (context, data),
                       dst,
                       src),
                   Ty_Void());
    }
    case A_retExp:
    {
        Semant_Exp sexp = TransExp (context, exp->u.ret);
        return Expression_New (Tr_Ret (context->level, sexp.exp), sexp.ty);
    }
    case A_callExp:
    {
        struct A_callExp_t callExp = exp->u.call;

        // query environment for the function name
        Env_Entry entry = (Env_Entry)S_Look (context->venv, callExp.func);
        if (!entry)
        {
            ERROR_UNKNOWN_SYMBOL (loc, callExp.func);
            return e_void;
        }
        else if (entry->kind != Env_funEntry)
        {
            ERROR (loc, 3006, "Symbol '%s' is not callable", S_Name (callExp.func));
            return e_void;
        }

        struct Env_funEntry_t fnEntry = entry->u.fun;

        // check arguments types with formal type list and if everything is ok translate
        Tr_expList tral = NULL;
        A_expList al = callExp.args;
        S_symbolList names = fnEntry.names;
        LIST_FOREACH (t, fnEntry.types)
        {
            if (!al)
            {
                ERROR (loc, 3007, "Missed a call argument '%s' of '%s'",
                       names->head->name,
                       t->meta.name);
            }
            else
            {
                Semant_Exp sexp = TransExp (context, al->head);

                if (sexp.ty != t)
                {
                    ERROR_UNEXPECTED_TYPE (&al->head->loc, t, sexp.ty);
                }

                LIST_PUSH (tral, sexp.exp);
            }

            names = LIST_NEXT (names);
            al = LIST_NEXT (al);
        }

        // check for superfluous arguments
        LIST_FOREACH (a, al)
        {
            ERROR (&a->loc, 3008, "Unexpected argument", "");
        }

        return Expression_New (Tr_Call (
                                   fnEntry.label,
                                   context->level,
                                   fnEntry.level,
                                   tral),
                               fnEntry.result);
    }
    case A_nilExp:
    {
        return Expression_New (Tr_Nil(), Ty_Nil());
    }
    case A_intExp:
    {
        return Expression_New (Tr_Int (exp->u.intt), Ty_Int());
    }
    case A_stringExp:
    {
        return Expression_New (Tr_String (context, exp->u.stringg), Ty_String());
    }
    case A_opExp:
    {
        struct A_opExp_t opExp = exp->u.op;

        A_oper oper = opExp.oper;

        Semant_Exp left = TransExp (context, opExp.left);
        Semant_Exp right = TransExp (context, opExp.right);

        // do type checks
        if (oper == A_plusOp
                || oper == A_minusOp
                || oper == A_timesOp
                || oper == A_divideOp
                || oper == A_gtOp
                || oper == A_ltOp
                || oper == A_geOp
                || oper == A_leOp
                || oper == A_eqOp
                || oper == A_neqOp)
        {
            /*
             * We do type recovery here to parse the unit further assuming these operations are
             * applicable for int only.
             */
            if (!is_int (left) || !is_int (right))
            {
                if (!is_int (left))
                {
                    ERROR_UNEXPECTED_TYPE (&opExp.left->loc, Ty_Int(), left.ty);
                    left.ty = Ty_Int();
                }
                if (!is_int (right))
                {
                    ERROR_UNEXPECTED_TYPE (&opExp.right->loc, Ty_Int(), right.ty);
                    right.ty = Ty_Int();
                }
            }

        }
        return Expression_New (Tr_Op (oper, left.exp, right.exp, left.ty), left.ty);
    }
    case A_arrayExp:
    // TODO store anonymous records
    case A_recordExp:
    {
        static Tr_access access = NULL;
        static int level = 0;
        static int thisOffset = 0;

        Ty_ty ty = NULL;
        Tr_exp ex = NULL;

        int nextOffset = 0;

        if (level == 0)
        {
            /*
             * We do not the real type yet but we need to pass the base further down the tree
             * so we create a virtual access point(base), later it will be updated with the
             * real type information.
             */
            access = Tr_AllocVirtual (context->level);
        }

        if (exp->kind == A_arrayExp)
        {
            if (exp->u.array == NULL)
            {
                ERROR_MALFORMED_EXP (&exp->loc, "Array expression cannot be empty");
                return e_invalid;
            }

            Tr_expList el = NULL;
            Ty_tyList tl = NULL;
            LIST_FOREACH (item, exp->u.array)
            {
                level++;
                Semant_Exp sexp = TransExp (context, item);
                level--;
                LIST_PUSH (el, sexp.exp);
                LIST_PUSH (tl, sexp.ty);

                int size = Ty_SizeOf (sexp.ty);
                thisOffset += size;
                nextOffset += size;
            }

            // returning to the current level offset
            thisOffset -= nextOffset;

            ty = tl->head;
            int size = 1;

            if (is_invalid (ty))
            {
                return e_invalid;
            }

            LIST_FOREACH (type, tl->tail)
            {
                size++;
                if (is_invalid (type))
                {
                    return e_invalid;
                }
                else if (type != ty)
                {
                    ERROR_MALFORMED_EXP (
                        &exp->loc,
                        "Array expression expects values of the same type");
                    return e_invalid;
                }
            }

            ty = Ty_Array (ty, size);
            ex = Tr_ArrayExp (access, ty, el, thisOffset);
        }
        else
        {

            if (exp->u.record.name)
            {
                ty = GetActualType ((Ty_ty)S_Look (context->tenv, exp->u.record.name));
                if (!ty)
                {
                    ERROR_UNKNOWN_TYPE (&exp->loc, exp->u.record.name);
                    return e_invalid;
                }
                if (is_invalid (ty))
                {
                    ERROR_INVALID_TYPE (&exp->loc, exp->u.record.name);
                    return e_invalid;
                }
            }

            Tr_expList el = NULL;
            Ty_fieldList fl = NULL;
            bool valid = TRUE;
            /*
             * This is offset(as part of thisOffset value) is used by the lower record level if
             * exists.
             */
            LIST_FOREACH (f, exp->u.record.fields)
            {
                level++;
                Semant_Exp sexp = TransExp (context, f->exp);
                level--;
                LIST_PUSH (el, sexp.exp);
                if (is_invalid (sexp.ty))
                {
                    valid = FALSE;
                }

                int size = Ty_SizeOf (sexp.ty);
                thisOffset += size;
                nextOffset += size;

                // creating name: type list
                LIST_PUSH (fl, Ty_Field (f->name, sexp.ty));
            }

            // returning to the current level offset
            thisOffset -= nextOffset;

            if (!valid)
            {
                ty = Ty_Invalid();
            }
            else if (ty)
            {
                Tr_expList ael = NULL;

                // checking whether the named record type can be initialized with the anon struct

                // TODO type checks for fuck sake
                // FIXME find fields that do not belong to the type
                nextOffset = 0;
                LIST_FOREACH (type_filed, ty->u.record)
                {
                    Tr_expList iel = el;
                    Tr_exp ie = NULL;

                    LIST_FOREACH (init_field, fl)
                    {
                        if (init_field->name == type_filed->name)
                        {
                            ie = iel->head;
                            break;
                        }

                        iel = iel->tail;
                    }

                    if (ie)
                    {
                        LIST_PUSH (ael, ie);
                    }
                    else
                    {
                        LIST_PUSH (ael, TransDefaultValue (
                                       access,
                                       type_filed->ty,
                                       thisOffset).exp);
                    }

                    int size = Ty_SizeOf (type_filed->ty);
                    thisOffset += size;
                    nextOffset += size;
                }

                // returning to the current level offset
                thisOffset -= nextOffset;

                el = ael;
            }
            // create anonymous type
            else if (!ty)
            {
                ty = Ty_Record (fl);
            }

            ex = Tr_RecordExp (access, ty, el, thisOffset);
        }


        if (level == 0)
        {
            /*
             * Now we know the type and the init tree is created using virtual access point,
             * we update the access
             */
            Tr_AllocMaterialize (access, context->level, ty, FALSE);
        }

        return Expression_New (ex, ty);
    }
    case A_assignExp:
    {
        struct A_assignExp_t assignExp = exp->u.assign;

        Semant_Exp lexp = TransVar (context, assignExp.var);
        Semant_Exp rexp = TransExp (context, assignExp.exp);

        // no way to recover from this
        if (is_invalid (lexp.ty) && is_invalid (rexp.ty))
        {
            ERROR_INVALID_EXPRESSION (&exp->loc)
            return e_invalid;
        }
        /*
         * partially valid expression, here we try to recover by mutually assigning correct
         * types in hope it won't break further analysis. We do not push any errors because they
         * were already fired by the lexp and rexp translations calls.
         */
        else if (is_invalid (lexp.ty))
        {
            lexp.ty = rexp.ty;
        }
        else if (is_invalid (rexp.ty))
        {
            rexp.ty = lexp.ty;
        }
        // correct but unmatched types, fix the type of the right expression
        else if (lexp.ty != rexp.ty)
        {
            ERROR_UNEXPECTED_TYPE (&assignExp.exp->loc, lexp.ty, rexp.ty)
            rexp.ty = lexp.ty;
        }

        return Expression_New (Tr_Assign (lexp.exp, rexp.exp), rexp.ty);
    }
    case A_ifExp:
    {
        struct A_ifExp_t ifExp = exp->u.iff;

        Semant_Exp texp = TransExp (context, ifExp.test);

        // currenlty Helium does not support bool
        if (texp.ty != Ty_Int())
        {
            ERROR_UNEXPECTED_TYPE (&ifExp.test->loc, Ty_Int(), texp.ty);
        }

        Semant_Exp pexp = e_void;
        if (ifExp.tr)
        {
            S_BeginScope (context->venv);
            S_BeginScope (context->tenv);

            pexp = TransScope (context, ifExp.tr);

            S_EndScope (context->venv);
            S_EndScope (context->tenv);
        }

        Semant_Exp nexp = e_void;
        if (ifExp.fl)
        {
            S_BeginScope (context->venv);
            S_BeginScope (context->tenv);

            nexp = TransScope (context, ifExp.fl);

            S_EndScope (context->venv);
            S_EndScope (context->tenv);
        }

        return Expression_New (Tr_If (texp.exp, pexp.exp, nexp.exp), Ty_Void());
    }
    case A_whileExp:
    {
        struct A_whileExp_t whileExp = exp->u.whilee;

        Semant_Exp texp = TransExp (context, whileExp.test);

        // currenlty Helium does not support bool
        if (texp.ty != Ty_Int())
        {
            ERROR_UNEXPECTED_TYPE (&whileExp.test->loc, Ty_Int(), texp.ty);
        }

        Temp_label outter_breaker = context->breaker;
        Temp_label breaker = Temp_NewLabel();
        context->breaker = breaker;
        context->loopNesting++;

        S_BeginScope (context->venv);
        S_BeginScope (context->tenv);

        Semant_Exp body = TransScope (context, whileExp.body);

        S_EndScope (context->venv);
        S_EndScope (context->tenv);

        context->loopNesting--;
        context->breaker = outter_breaker;

        return Expression_New (Tr_While (texp.exp, body.exp, breaker), Ty_Void());
    }
    case A_forExp:
    {
        struct A_forExp_t forExp = exp->u.forr;

        Semant_Exp lexp = TransExp (context, forExp.lo);
        if (!is_int (lexp))
        {
            ERROR_UNEXPECTED_TYPE (&forExp.lo->loc, Ty_Int(), lexp.ty)
        }

        Semant_Exp hexp = TransExp (context, forExp.hi);
        if (!is_int (hexp))
        {
            ERROR_UNEXPECTED_TYPE (&forExp.hi->loc, Ty_Int(), hexp.ty)
        }

        S_BeginScope (context->venv);
        S_BeginScope (context->tenv);

        // store the iterator within the new scope
        Env_Entry entry = Env_VarEntryNew (
                              Tr_Alloc (context->level, Ty_Int(), forExp.escape),
                              Ty_Int());
        S_Enter (context->venv, forExp.var, entry);

        Temp_label label = context->breaker;
        Temp_label breaker = Temp_NewLabel();
        context->breaker = breaker;
        context->loopNesting++;

        Semant_Exp body = TransScope (context, forExp.body);

        context->loopNesting--;
        context->breaker = label;

        S_EndScope (context->venv);
        S_EndScope (context->tenv);

        return Expression_New (Tr_For (
                                   lexp.exp,
                                   hexp.exp,
                                   body.exp,
                                   entry->u.var.access,
                                   breaker),
                               Ty_Void());
    }
    case A_breakExp:
    {
        assert (context->loopNesting >= 0);

        if (!context->loopNesting)
        {
            ERROR (&exp->loc, 3010, "Unexpected break", "");
            return e_void;
        }
        else
        {
            return Expression_New (Tr_Break (context->breaker), Ty_Void());
        }
    }
    default:
    {
        assert (0);
    }
    }

    return e_invalid;
}

int Semant_Translate (Program_Module m)
{
    assert (m);

    Escape_Find (m->ast);

    struct Semant_ContextType context;
    context.module = m;
    context.loopNesting = 0;

    Env_Init (&context);
    Tr_Init (&context);

    LIST_FOREACH (dec, m->ast) TransDec (&context, dec);

    return Vector_Size (&m->errors.semant);
}
