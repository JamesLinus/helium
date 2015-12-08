#include <assert.h>

#include "ext/str.h"

#include "ast.h"
#include "preproc.h"

typedef struct PreProc_Context_t
{
    Program_Module module;
    S_table tenv;

} * PreProc_Context;

static A_exp TransExp (PreProc_Context context, A_exp exp);
static A_dec TransDec (PreProc_Context context, A_dec dec);

static A_scope TransScope (PreProc_Context context, A_scope scope)
{
    LIST_FOREACH (stm, scope->list)
    {
        switch (stm->kind)
        {
        case A_stmExp:
        {
            stm->u.exp = TransExp (context, stm->u.exp);
            break;
        }
        case A_stmDec:
        {
            stm->u.dec = TransDec (context, stm->u.dec);
            break;
        }
        }
    }

    return scope;
}

static A_exp TransExp (PreProc_Context context, A_exp exp)
{
    switch (exp->kind)
    {
    case A_seqExp:
    {
        if (!exp->u.seq)
        {
            return exp;
        }

        A_expList l = NULL;
        LIST_FOREACH (e, exp->u.seq)
        {
            LIST_PUSH (l, TransExp (context, e));
        }

        exp->u.seq = l;
        return exp;
    }
    case A_ifExp:
    {
        if (exp->u.iff.test)
        {
            TransExp (context, exp->u.iff.test);
        }

        if (exp->u.iff.tr)
        {
            TransScope (context, exp->u.iff.tr);
        }

        if (exp->u.iff.fl)
        {
            TransScope (context, exp->u.iff.fl);
        }

        return exp;
    }
    case A_whileExp:
    {
        if (exp->u.whilee.test)
        {
            TransExp (context, exp->u.whilee.test);
        }

        if (exp->u.whilee.body)
        {
            TransScope (context, exp->u.whilee.body);
        }

        return exp;
    }
    case A_forExp:
    {
        if (exp->u.forr.hi)
        {
            TransExp(context, exp->u.forr.hi);
        }

        if (exp->u.forr.lo)
        {
            TransExp(context, exp->u.forr.lo);
        }

        if (exp->u.forr.body)
        {
            TransScope(context, exp->u.forr.body);
        }

        return exp;
    }
    // TODO real asm parser is badly needed
    // TODO drop pseudo
    // TODO i need to be able use data address directly
    case A_macroCallExp:
    {
        // TODO Add 10000 code for runtime panic
        struct String_t name = String (exp->u.macro.name->name);

        if (String_Equal (&name, "panic"))
        {
            char * msg = checked_malloc (1024);
            sprintf (msg, "EXIT %d,%d 10000 %s",
                     exp->loc.first_line,
                     exp->loc.first_column,
                     exp->u.macro.args->head->u.stringg);

            A_expList l = NULL;

            // show message
            LIST_PUSH (l, A_AsmExp (&exp->loc, "li $v0, 4", NULL,
                                    U_StringList ("$v0", NULL), NULL));

            LIST_PUSH (l, A_AsmExp (&exp->loc, "add $a0, `s0, $zero", msg,
                                    U_StringList ("$zero", NULL), NULL));

            LIST_PUSH (l, A_AsmExp (&exp->loc, "syscall", NULL, NULL,
                                    U_StringList ("$v0",
                                                  U_StringList ("$a0", NULL))));

            // exit program with status code 1
            LIST_PUSH (l, A_AsmExp (&exp->loc, "li $v0, 17", NULL,
                                    U_StringList ("$v0", NULL), NULL));

            LIST_PUSH (l, A_AsmExp (&exp->loc, "li $a0, 1", NULL,
                                    U_StringList ("$a0", NULL), NULL));

            LIST_PUSH (l, A_AsmExp (&exp->loc, "syscall", NULL, NULL,
                                    U_StringList ("$v0",
                                                  U_StringList ("$a0", NULL))));

            // this makes the whole panic macro evaluate to 0(OK)
            LIST_PUSH (l, A_IntExp (&exp->loc, 0));

            exp->kind  = A_seqExp;
            exp->u.seq = l;

            return exp;
        }
        else if (String_Equal (&name, "assert"))
        {
            A_exp test = exp->u.macro.args->head;
            A_exp panic = A_MacroCallExp (
                              &test->loc,
                              S_Symbol ("panic"),
                              A_ExpList (A_StringExp (&test->loc, "Assert failed!"), NULL));

            A_scope fl = A_Scope (A_StmList (A_StmExp (TransExp (context, panic)), NULL));

            exp = A_IfExp (&test->loc, test, NULL, fl);

            // this makes the whole assert macro evaluate to 0(OK)
            A_exp zero = A_IntExp (&test->loc, 0);
            return A_SeqExp (&test->loc, A_ExpList (exp, A_ExpList (zero, NULL)));
        }
        else
        {
            assert (0);
        }
        break;
    }
    default:
    {
        return exp;
    }
    }
}

static A_dec TransDec (PreProc_Context context, A_dec dec)
{
    switch (dec->kind)
    {
    case A_functionDec:
    {
        dec->u.function.scope = TransScope (context, dec->u.function.scope);
        return dec;
    }
    default:
    {
        return dec;
    }
    }
}

int PreProc_Translate (Program_Module m)
{
    assert (m);

    struct PreProc_Context_t context;

    // Pre Processor will replace all macro nodes with the actul expressions
    LIST_FOREACH (dec, m->ast) TransDec (&context, dec);

    return Vector_Size (&m->errors.semant);
}
