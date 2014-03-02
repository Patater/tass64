/*
    $Id$

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include "eval.h"
#include <string.h>
#include <math.h>
#include "file.h"
#include "section.h"
#include "encoding.h"
#include "macro.h"
#include "variables.h"
#include "64tass.h"
#include "misc.h"

#include "listobj.h"
#include "floatobj.h"
#include "strobj.h"
#include "codeobj.h"
#include "addressobj.h"
#include "bytesobj.h"
#include "intobj.h"
#include "boolobj.h"

#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
#else
#define cbrt(a) pow((a), 1.0/3.0)
#endif 
#if _XOPEN_SOURCE >= 600 || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
#else
double round(double);
double trunc(double);
inline double round(double a) {return (a < 0.0) ? ceil(a-0.5) : floor(a+0.5);}
inline double trunc(double a) {return (a > 0.0) ? floor(a) : ceil(a);}
#endif 
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 600 || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
#else
double sinh(double);
double cosh(double);
double tanh(double);
inline double sinh(double a) {return (exp(a) - exp(-a)) / 2.0;}
inline double cosh(double a) {return (exp(a) + exp(-a)) / 2.0;}
inline double tanh(double a) {return sinh(a) / cosh(a);}
#endif
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
#else
double hypot(double, double);
inline double hypot(double a, double b) {return sqrt(a*a+b*b);}
#endif

size_t get_label(void) {
    uint8_t ch;
    struct linepos_s e;
    if (here()>='0' && here()<='9') return 0;
    e.pos = lpoint.pos;
    while ((((ch=here()) | 0x20) >= 'a' && (ch | 0x20) <= 'z') || (ch>='0' && ch<='9') || ch=='_') lpoint.pos++;
    return lpoint.pos - e.pos;
}

static void get_dec(struct value_s *v) {
    while (here() == 0x30) lpoint.pos++;
    lpoint.pos += int_from_decstr(v, pline + lpoint.pos);
}

static void get_exponent(struct value_s *v, double real, struct value_s *err) {
    int base;

    v->obj->destroy(v); 
    switch (here() | 0x20) {
    case 'p': base = 2; break;
    case 'e': base = 10; break;
    default: base = 0;
    }
    if (base) {
        int neg = 0;
        neg = (pline[lpoint.pos + 1] == '-');
        if (neg || pline[lpoint.pos + 1] == '+') {
            if ((pline[lpoint.pos + 2] ^ 0x30) < 10) lpoint.pos++;
        }
        if ((pline[lpoint.pos + 1] ^ 0x30) < 10) {
            ival_t expo;
            struct value_s tmp;
            size_t len;
            lpoint.pos++;

            len = int_from_decstr(&tmp, pline + lpoint.pos);
            if (tmp.obj->ival(&tmp, err, &expo, 8*sizeof(uval_t), &lpoint)) expo = 0;
            tmp.obj->destroy(&tmp);
            lpoint.pos += len;

            if (neg) expo = -expo;
            if (expo) real *= pow(base, expo);
        }
    }
    if (err->obj != NONE_OBJ) {
        err->obj->copy_temp(err, v);
        return;
    }
    v->obj = FLOAT_OBJ;
    v->u.real = real;
}

static void get_exponent2(struct value_s *v) {
    struct value_s err;
    double real;
    switch (here() | 0x20) {
    case 'e':
    case 'p':
        if (pline[lpoint.pos + 1] == '-' || pline[lpoint.pos + 1] == '+') {
            if ((pline[lpoint.pos + 2] ^ 0x30) < 10) {
                err.obj = NONE_OBJ;
                if (v->obj->real(v, &err, &real, &lpoint)) real = 0;
                return get_exponent(v, real, &err);
            }
        } else if ((pline[lpoint.pos + 1] ^ 0x30) < 10) {
            err.obj = NONE_OBJ;
            if (v->obj->real(v, &err, &real, &lpoint)) real = 0;
            return get_exponent(v, real, &err);
        }
    default: break;
    }
    return;
}

static void get_hex(struct value_s *v) {
    struct value_s tmp, err;
    size_t len = bits_from_hexstr(v, pline + lpoint.pos);

    if (pline[lpoint.pos + len] == '.' && pline[lpoint.pos + len + 1] != '.') {
        double real, real2;
        err.obj = NONE_OBJ;
        if (v->obj->real(v, &err, &real, &lpoint)) real = 0;
        lpoint.pos += len + 1;

        len = bits_from_hexstr(&tmp, pline + lpoint.pos);
        if (tmp.obj->real(&tmp, &err, &real2, &lpoint)) real2 = 0;
        tmp.obj->destroy(&tmp);
        lpoint.pos += len;

        if (real2) real += real2 * pow(16.0, -(double)len);
        return get_exponent(v, real, &err);
    }
    lpoint.pos += len;
    return get_exponent2(v);
}

static void get_bin(struct value_s *v) {
    struct value_s tmp, err;
    size_t len = bits_from_binstr(v, pline + lpoint.pos);

    if (pline[lpoint.pos + len] == '.' && pline[lpoint.pos + len + 1] != '.') {
        double real, real2;
        err.obj = NONE_OBJ;
        if (v->obj->real(v, &err, &real, &lpoint)) real = 0;
        lpoint.pos += len + 1;

        len = bits_from_binstr(&tmp, pline + lpoint.pos);
        if (tmp.obj->real(&tmp, &err, &real2, &lpoint)) real2 = 0;
        tmp.obj->destroy(&tmp);
        lpoint.pos += len;

        if (real2) real += real2 * pow(2.0, -(double)len);
        return get_exponent(v, real, &err);
    }
    lpoint.pos += len;
    return get_exponent2(v);
}

static void get_float(struct value_s *v) {
    size_t len;
    struct value_s tmp, err;

    while (here() == 0x30) lpoint.pos++;
    len = int_from_decstr(v, pline + lpoint.pos);
    if (pline[lpoint.pos + len] == '.' && pline[lpoint.pos + len + 1] != '.') {
        double real, real2;
        err.obj = NONE_OBJ;
        if (v->obj->real(v, &err, &real, &lpoint)) real = 0;
        lpoint.pos += len + 1;

        len = int_from_decstr(&tmp, pline + lpoint.pos);
        if (tmp.obj->real(&tmp, &err, &real2, &lpoint)) real2 = 0;
        tmp.obj->destroy(&tmp);
        lpoint.pos += len;

        if (real2) real += real2 * pow(10.0, -(double)len);
        get_exponent(v, real, &err);
        return;
    }
    lpoint.pos += len;
    return get_exponent2(v);
}

static void get_string(struct value_s *v) {
    lpoint.pos += str_from_str(v, &lpoint.upos, pline + lpoint.pos);
}

void touch_label(struct label_s *tmp) {
    if (referenceit) tmp->ref = 1;
    tmp->usepass = pass;
}

static void get_star(struct value_s *v) {
    struct star_s *tmp;
    int labelexists;

    tmp=new_star(vline, &labelexists);
    if (labelexists && tmp->addr != star) {
        if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, &lpoint);
        fixeddig = 0;
    }
    tmp->addr=star;
    v->obj = ADDRESS_OBJ;
    v->u.addr.type = A_NONE;
    v->u.addr.val = val_alloc();
    int_from_uval(v->u.addr.val, star);
}

static size_t evxnum, evx_p;
static struct eval_context_s {
    struct values_s *values;
    size_t values_size;
    size_t outp, outp2;
    int gstop;
    struct values_s *o_out;
    size_t out_size;
} **evx;

static struct eval_context_s *eval;


static inline struct value_s *push(linepos_t epoint) {
    struct values_s *o_out;
    if (eval->outp >= eval->out_size) {
        size_t i;
        eval->out_size += 64;
        eval->o_out = (struct values_s *)realloc(eval->o_out, eval->out_size * sizeof(eval->o_out[0]));
        if (!eval->o_out || eval->out_size < 64 || eval->out_size > SIZE_MAX / sizeof(eval->o_out[0])) err_msg_out_of_memory(); /* overflow */
        for (i = eval->outp; i < eval->out_size; i++) eval->o_out[i].val = &none_value;
    }
    o_out = &eval->o_out[eval->outp++];
    o_out->epoint = *epoint;
    return val_realloc(&o_out->val);
}

static inline void push_oper(struct value_s *val, linepos_t epoint) {
    if (eval->outp >= eval->out_size) {
        size_t i;
        eval->out_size += 64;
        eval->o_out = (struct values_s *)realloc(eval->o_out, eval->out_size * sizeof(eval->o_out[0]));
        if (!eval->o_out || eval->out_size < 64 || eval->out_size > SIZE_MAX / sizeof(eval->o_out[0])) err_msg_out_of_memory(); /* overflow */
        for (i = eval->outp; i < eval->out_size; i++) eval->o_out[i].val = &none_value;
    } else val_destroy(eval->o_out[eval->outp].val);
    eval->o_out[eval->outp].val = val;
    eval->o_out[eval->outp++].epoint = *epoint;
}

static int get_exp_compat(int *wd, int stop) {/* length in bytes, defined */
    char ch;

    struct value_s *conv, *conv2;
    struct values_s o_oper[256];
    uint8_t operp = 0;
    struct linepos_s epoint, cpoint = {0, 0, 0};
    struct value_s *val;
    size_t llen;
    int first;
    str_t ident;
    struct label_s *l;

    *wd=3;    /* 0=byte 1=word 2=long 3=negative/too big */

    eval->outp = 0;
    o_oper[0].val = &o_SEPARATOR;
rest:
    ignore();
    conv = conv2 = NULL;
    first = (here() == '(') && (stop == 3 || stop == 4);
    if (!eval->outp && here() == '#') {
        conv2 = &o_HASH; lpoint.pos++;
    }
    switch (here()) {
    case 0:
    case ';': err_msg(ERROR_MISSING_ARGUM,NULL); return 0;
    case '!':*wd=1;lpoint.pos++;break;
    case '<': conv = &o_LOWER; cpoint = lpoint; lpoint.pos++;break; 
    case '>': conv = &o_HIGHER;cpoint = lpoint; lpoint.pos++;break; 
    }
    for (;;) {
        ignore();ch = here(); epoint=lpoint;

        switch (ch) {
        case '(': lpoint.pos++;o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_PARENT;continue;
        case '$': lpoint.pos++;val = push(&epoint);get_hex(val);goto other;
        case '%': lpoint.pos++;val = push(&epoint);get_bin(val);goto other;
        case '"': val = push(&epoint);get_string(val);goto other;
        case '*': lpoint.pos++;val = push(&epoint);get_star(val);goto other;
        }
        if (ch>='0' && ch<='9') {val = push(&epoint); get_dec(val);goto other;} 
        if (!get_label()) goto syntaxe;
    as_ident:
        ident.data = pline + epoint.pos;
        ident.len = lpoint.pos - epoint.pos;
        l = find_label(&ident);
        if (l) {
            touch_label(l);
            l->shadowcheck = 1;
            push_oper(val_reference(l->value), &epoint);
        } else {
            val = push(&epoint);
            val->obj = ERROR_OBJ;
            val->u.error.num = ERROR___NOT_DEFINED;
            val->u.error.epoint = epoint;
            val->u.error.u.notdef.ident = ident;
            val->u.error.u.notdef.label = current_context;
            val->u.error.u.notdef.down = 1;
        }
    other:
        if (stop != 2) ignore();
        ch = here(); epoint=lpoint;

        while (operp && o_oper[operp-1].val != &o_PARENT) {
            operp--;
            push_oper(o_oper[operp].val, &o_oper[operp].epoint);
        }
        switch (ch) {
        case ',':
            lpoint.pos++;
            llen = get_label();
            if (llen == 1) {
                uint8_t c = pline[epoint.pos + 1];
                if (!arguments.casesensitive) c |= 0x20;
                switch (c) {
                case 'x': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_COMMAX;goto other;
                case 'y': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_COMMAY;goto other;
                default: break;
                }
            }
            if (conv) push_oper(conv, &cpoint);
            if (conv2) push_oper(conv2, &cpoint);
            if (stop == 1 || stop == 4) {lpoint = epoint;break;}
            push_oper(&o_SEPARATOR, &epoint);
            if (llen) {
                epoint.pos++;
                goto as_ident;
            }
            goto rest;
        case '&': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_AND; lpoint.pos++;continue;
        case '.': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_OR; lpoint.pos++;continue;
        case ':': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_XOR; lpoint.pos++;continue;
        case '*': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_MUL; lpoint.pos++;continue;
        case '/': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_DIV; lpoint.pos++;continue;
        case '+': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_ADD; lpoint.pos++;continue;
        case '-': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_SUB; lpoint.pos++;continue;
        case ')':
            if (!operp) {err_msg(ERROR______EXPECTED,"("); goto error;}
            lpoint.pos++;
            operp--;
            if (first) {
                o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_TUPLE;
                first = 0;
            }
            goto other;
        case 0:
        case ';':
        case '\t':
        case ' ':
            if (conv) push_oper(conv, &cpoint);
            if (conv2) push_oper(conv2, &cpoint);
            break;
        default: goto syntaxe;
        }
        if (!operp) return 1;
        err_msg(ERROR______EXPECTED,")"); goto error;
    syntaxe:
        err_msg(ERROR_EXPRES_SYNTAX,NULL);
    error:
        return 0;
    }
    return 0;
}

static int get_val2_compat(struct eval_context_s *ev) {/* length in bytes, defined */
    size_t vsp = 0;
    enum oper_e op;
    const struct value_s *op2;
    size_t i;
    struct value_s *val, new_value;
    struct values_s *v1, *v2;
    struct values_s *o_out;
    struct values_s *values;

    if (ev->outp2 >= ev->outp) return 1;
    values = ev->values;

    for (i = ev->outp2; i < ev->outp; i++) {
        o_out = &ev->o_out[i];
        val = o_out->val;
        if (val->obj != OPER_OBJ) {
            if (vsp >= ev->values_size) {
                size_t j = ev->values_size;
                ev->values_size += 16;
                ev->values = values = (struct values_s *)realloc(ev->values, ev->values_size * sizeof(struct values_s));
                if (!values || ev->values_size < 16 || ev->values_size > SIZE_MAX / sizeof(struct values_s)) err_msg_out_of_memory(); /* overflow */
                for (; j < ev->values_size; j++) ev->values[j].val = &none_value;
            }
            val_destroy(values[vsp].val);
            values[vsp].val = val;
            o_out->val = &none_value;
            values[vsp++].epoint = o_out->epoint;
            continue;
        }
        op2 = val;
        op = op2->u.oper.op;

        if (op == O_SEPARATOR) {
            ev->outp2 = i + 1;
            return 0;
        }
        if (vsp < 1) goto syntaxe;
        v1 = &values[vsp-1];
        switch (op) {
        case O_LOWER:
        case O_HIGHER:
        case O_HASH:
        case O_COMMAX:
        case O_COMMAY:
        case O_TUPLE:
            switch (v1->val->obj->type) {
            case T_ADDRESS:
                switch (op) {
                case O_COMMAX:
                case O_COMMAY:
                case O_TUPLE:
                    new_value.obj = ADDRESS_OBJ;
                    new_value.u.addr.type = v1->val->u.addr.type << 4;
                    new_value.u.addr.type |= (op == O_TUPLE) ? A_I : (op == O_COMMAX) ? A_XR : A_YR;
                    new_value.u.addr.val = val_reference(v1->val->u.addr.val);
                    val_replace_template(&v1->val, &new_value);
                    v1->epoint = o_out->epoint;
                    continue;
                default:break;
                }
                err_msg_invalid_oper(op2, v1->val, NULL, &o_out->epoint);
                val_replace(&v1->val, &none_value); 
                break;
            case T_CODE:
            case T_INT:
            case T_BITS:
            case T_STR:
            case T_BOOL:
                {
                    uint16_t val1;
                    uval_t uval;
                    if (v1->val->obj->uval(v1->val, &new_value, &uval, 8*sizeof(uval_t), &v1->epoint)) {
                        val_replace_template(&v1->val, &new_value);
                        break;
                    }
                    val1 = uval;

                    switch (op) {
                    case O_HASH:
                    case O_COMMAX:
                    case O_COMMAY:
                        new_value.obj = ADDRESS_OBJ;
                        new_value.u.addr.type = (op == O_HASH) ? A_IMMEDIATE : (op == O_COMMAX) ? A_XR : A_YR;
                        new_value.u.addr.val = val_reference(v1->val);
                        val_replace_template(&v1->val, &new_value);
                        v1->epoint = o_out->epoint;
                        continue;
                    case O_HIGHER: val1 >>= 8;
                    case O_LOWER: val1 = (uint8_t)val1;break;
                    case O_TUPLE:
                        new_value.obj = ADDRESS_OBJ;
                        new_value.u.addr.type = A_I;
                        new_value.u.addr.val = val_reference(v1->val);
                        val_replace_template(&v1->val, &new_value);
                        v1->epoint = o_out->epoint;
                        continue;
                    default: break;
                    }
                    int_from_uval(&new_value, val1);
                    val_replace_template(&v1->val, &new_value);
                    break;
                }
            default:
                err_msg_invalid_oper(op2, v1->val, NULL, &o_out->epoint);
                val_replace(&v1->val, &none_value); 
            case T_ERROR:
            case T_NONE:break;
            }
            v1->epoint = o_out->epoint;
            continue;
        default:break;
        }
        if (vsp < 2) {
        syntaxe:
            err_msg(ERROR_EXPRES_SYNTAX,NULL);
            ev->outp2 = ev->outp;
            return -1;
        }
        v2 = &values[vsp-2];
        switch (v1->val->obj->type) {
        case T_INT:
        case T_BITS:
        case T_CODE:
        case T_STR:
        case T_BOOL:
        case T_ADDRESS:
            switch (v2->val->obj->type) {
            case T_INT:
            case T_BITS:
            case T_CODE:
            case T_STR:
            case T_BOOL:
            case T_ADDRESS:
                {
                    uint16_t val1, val2;
                    uval_t uval;
                    if (v1->val->obj->uval(v1->val, &new_value, &uval, 8*sizeof(uval_t), &v1->epoint)) {
                        vsp--;
                        val_replace_template(&v2->val, &new_value);
                        continue;
                    }
                    val1 = uval;
                    if (v2->val->obj->uval(v2->val, &new_value, &uval, 8*sizeof(uval_t), &v2->epoint)) {
                        vsp--;
                        val_replace_template(&v2->val, &new_value);
                        continue;
                    }
                    val2 = uval;

                    switch (op) {
                    case O_MUL: val1 *= val2; break;
                    case O_DIV: if (!val1) {
                        new_value.obj = ERROR_OBJ;
                        new_value.u.error.num = ERROR_DIVISION_BY_Z;
                        new_value.u.error.epoint = v1->epoint;
                        val_replace_template(&v2->val, &new_value);
                        continue;
                    } else val1=val2 / val1; break;
                    case O_ADD: val1 += val2; break;
                    case O_SUB: val1 = val2 - val1; break;
                    case O_AND: val1 &= val2; break;
                    case O_OR:  val1 |= val2; break;
                    case O_XOR: val1 ^= val2; break;
                    default: break;
                    }
                    vsp--;
                    int_from_uval(&new_value, val1);
                    val_replace_template(&v2->val, &new_value);
                    continue;
                }
            default: err_msg_invalid_oper(op2, v2->val, v1->val, &o_out->epoint);
            case T_ERROR:
            case T_NONE:break;
            }
            break;
        default:
            err_msg_invalid_oper(op2, v2->val, v1->val, &o_out->epoint);
        case T_ERROR:
        case T_NONE:break;
        }
        vsp--; val_replace(&v2->val, &none_value); continue;
    }
    ev->outp2 = i;
    return 0;
}

int eval_finish(void) {
    if (eval->outp2 < eval->outp) {
        lpoint = eval->o_out[eval->outp2].epoint;
        eval->outp2 = eval->outp;
        return 1;
    }
    return 0;
}

static int get_val2(struct eval_context_s *);

struct value_s *get_val(struct linepos_s *epoint) {/* length in bytes, defined */
    int res;
    struct values_s *value;

    if (arguments.tasmcomp) {
        res = get_val2_compat(eval);
    } else {
        res = get_val2(eval);
    }
    if (res) return (res > 0) ? NULL : &none_value;

    value = eval->values;

    if (epoint) *epoint = value->epoint;

    if (value->val->obj == ERROR_OBJ) {
        err_msg_output_and_destroy(value->val);
        value->val->obj = NONE_OBJ;
        return &none_value;
    }
    return value->val;
}

static void functions(struct values_s *vals, unsigned int args) {
    struct values_s *v = &vals[2];

    switch (vals->val->obj->type) {
    case T_FUNCTION: return builtin_function(vals, args, vals->val->u.function.func);
    case T_MFUNC:
        {
            struct value_s *val;
            unsigned int i;
            size_t max = 0;
            for (i = 0; i < args; i++) {
                if (v[i].val->obj == NONE_OBJ || v[i].val->obj == ERROR_OBJ) {
                    val_replace(&vals->val, v[i].val);
                    return;
                }
            }
            for (; i < vals->val->u.mfunc.argc; i++) {
                if (!vals->val->u.mfunc.param[i].init) {
                    max = i + 1;
                }
            }
            if (max) err_msg_argnum(args, max, vals->val->u.mfunc.argc, &vals->epoint);
            eval_enter();
            val = mfunc2_recurse(vals->val, v, args, &vals->epoint);
            eval_leave();
            if (!val) val = &null_tuple;
            val_destroy(vals->val);
            vals->val = val;
        }
        break;
    default:
        err_msg_invalid_oper(&o_FUNC, vals->val, NULL, &vals->epoint);
        val_replace(&vals->val, &none_value);
    case T_ERROR:
    case T_NONE: break;
    }
}

ival_t indexoffs(const struct value_s *v1, struct value_s *v, size_t len, linepos_t epoint) {
    ival_t ival;
    if (v1->obj->ival(v1, v, &ival, 8*sizeof(ival_t), epoint)) {
        return -1;
    }

    if (ival >= 0) {
        if ((size_t)ival < len) return ival;
    } else {
        if ((size_t)-ival <= len) return len + ival;
    }
    v->obj = ERROR_OBJ;
    v->u.error.num = ERROR___INDEX_RANGE;
    v->u.error.epoint = *epoint;
    return -1;
}

ival_t sliceparams(oper_t op, size_t len, ival_t *offs2, ival_t *end2, ival_t *step2) {
    struct value_s *v1 = op->v1, *v2 = op->v2, *v = op->v, tmp;
    ival_t ln, offs, end, step = 1;
    if (v2->u.list.len > 3 || v2->u.list.len < 1) {
        err_msg_argnum(v2->u.list.len, 1, 3, &op->epoint2);
        if (v1 == v || v2 == v) v->obj->destroy(v);
        v->obj = NONE_OBJ;
        return -1;
    }
    end = (ival_t)len;
    if (v2->u.list.len > 2) {
        if (v2->u.list.data[2]->obj != DEFAULT_OBJ) {
            if (v2->u.list.data[2]->obj->ival(v2->u.list.data[2], &tmp, &step, 8*sizeof(ival_t), &op->epoint2)) {
                if (v1 == v || v2 == v) v->obj->destroy(v);
                tmp.obj->copy_temp(&tmp, op->v);
                return -1;
            }
            if (step == 0) {
                if (v1 == v) v->obj->destroy(v);
                v->obj = ERROR_OBJ;
                v->u.error.num = ERROR_DIVISION_BY_Z;
                v->u.error.epoint = op->epoint2;
                return -1;
            }
        }
    }
    if (v2->u.list.len > 1) {
        if (v2->u.list.data[1]->obj == DEFAULT_OBJ) end = (step > 0) ? (ival_t)len : -1;
        else {
            if (v2->u.list.data[1]->obj->ival(v2->u.list.data[1], &tmp, &end, 8*sizeof(ival_t), &op->epoint2)) {
                if (v1 == v || v2 == v) v->obj->destroy(v);
                tmp.obj->copy_temp(&tmp, op->v);
                return -1;
            }
            if (end >= 0) {
                if (end > (ival_t)len) end = len;
            } else {
                if (end < 0) end += len;
            }
            if (end < -1) end = -1;
        }
    } else end = len;
    if (v2->u.list.data[0]->obj == DEFAULT_OBJ) offs = (step > 0) ? 0 : len - 1;
    else {
        if (v2->u.list.data[0]->obj->ival(v2->u.list.data[0], &tmp, &offs, 8*sizeof(ival_t), &op->epoint2)) {
            if (v1 == v || v2 == v) v->obj->destroy(v);
            tmp.obj->copy_temp(&tmp, op->v);
            return -1;
        }
        if (offs >= 0) {
            if (offs > (ival_t)len - (step < 0)) offs = len - (step < 0);
        } else {
            if (offs < 0) offs += len;
        }
        if (offs < - (step < 0)) offs = - (step < 0);
    }

    if (step > 0) {
        if (offs > end) offs = end;
        ln = (end - offs + step - 1) / step;
    } else {
        if (end > offs) end = offs;
        ln = (offs - end - step - 1) / -step;
    }

    *offs2 = offs;
    *end2 = end;
    *step2 = step;
    return ln;
}

static void indexes(struct values_s *vals, unsigned int args) {
    struct values_s *v = &vals[2];

    switch (vals->val->obj->type) {
    case T_ERROR:
    case T_NONE: return;
    default: break;
    }
    if (args != 1) err_msg_argnum(args, 1, 1, &vals[1].epoint); else {
        struct oper_s oper;
        oper.op = &o_INDEX;
        oper.v1 = vals->val;
        oper.v2 = v[0].val;
        oper.epoint = vals->epoint;
        oper.epoint2 = v[0].epoint;
        oper.epoint3 = vals[1].epoint;
        if (vals->val->refcount != 1) {
            oper.v = val_alloc();
            oper.v1->obj->iindex(&oper);
            val_destroy(vals->val); vals->val = oper.v;
        } else {
            oper.v = oper.v1;
            oper.v1->obj->iindex(&oper);
        }
        return;
    }
    return val_replace(&vals->val, &none_value);
}

static inline void apply_op2(oper_t op) {
    if (op->op == &o_X) {
        ival_t shift;
        struct value_s tmp;
        if (op->v2->obj->ival(op->v2, &tmp, &shift, 8*sizeof(ival_t), &op->epoint2)) {
            if (op->v1 == op->v || op->v2 == op->v) op->v->obj->destroy(op->v);
            tmp.obj->copy_temp(&tmp, op->v);
            return;
        }
        op->v1->obj->repeat(op, (shift > 0) ? shift : 0); return;
    }
    else op->v1->obj->calc2(op);
}

static struct value_s *apply_addressing(struct value_s *v1, struct value_s *v, enum atype_e am) {
    size_t i;
    struct value_s **vals, *tmp;

    switch (v1->obj->type) {
    case T_ADDRESS:
        v->obj = ADDRESS_OBJ;
        if (v1 != v) v->u.addr.val = val_reference(v1->u.addr.val);
        v->u.addr.type = am | (v1->u.addr.type << 4);
        return NULL;
    case T_LIST:
    case T_TUPLE:
        if (v1 == v) {
            for (i = 0; i < v1->u.list.len; i++) {
                if (v1->u.list.data[i]->refcount != 1) {
                    tmp = val_alloc();
                    apply_addressing(v1->u.list.data[i], tmp, am);
                    val_destroy(v1->u.list.data[i]); v1->u.list.data[i] = tmp;
                } else {
                    tmp = apply_addressing(v1->u.list.data[i], v1->u.list.data[i], am);
                    if (tmp) v1->u.list.data[i] = tmp;
                }
            }
            return NULL;
        } 
        vals = list_create_elements(v, v1->u.list.len);
        for (i = 0; i < v1->u.list.len; i++) {
            vals[i] = val_alloc();
            apply_addressing(v1->u.list.data[i], vals[i], am);
        }
        v->obj = v1->obj;
        v->u.list.len = v1->u.list.len;
        v->u.list.data = vals;
        return NULL;
    case T_ERROR:
        err_msg_output(v1);
        if (v1 == v) {
            v->obj->destroy(v);
            v->obj = NONE_OBJ;
        } else v1 = &none_value;
        /* fall through */
    default:
        if (v1 == v) {
            tmp = val_alloc();
            tmp->obj = ADDRESS_OBJ;
            tmp->u.addr.val = v1;
            tmp->u.addr.type = am;
            return tmp;
        }
        v->obj = ADDRESS_OBJ;
        v->u.addr.val = val_reference(v1);
        v->u.addr.type = am;
        return NULL;
    }
}

static int get_val2(struct eval_context_s *ev) {
    size_t vsp = 0;
    size_t i;
    enum oper_e op;
    const struct value_s *op2;
    struct values_s *v1, *v2;
    int stop = ev->gstop == 3 || ev->gstop == 4;
    struct values_s *o_out;
    struct value_s *val;
    struct value_s new_value;
    struct values_s *values;
    struct oper_s oper;
    atype_t am;

    if (ev->outp2 >= ev->outp) return 1;
    values = ev->values;

    for (i = ev->outp2; i < ev->outp; i++) {
        o_out = &ev->o_out[i];
        val = o_out->val;
        if (val->obj != OPER_OBJ || val == &o_PARENT || val == &o_BRACKET || val == &o_BRACE) {
            if (vsp >= ev->values_size) {
                size_t j = ev->values_size;
                ev->values_size += 16;
                ev->values = values = (struct values_s *)realloc(values, ev->values_size * sizeof(struct values_s));
                if (!values || ev->values_size < 16 || ev->values_size > SIZE_MAX / sizeof(struct values_s)) err_msg_out_of_memory(); /* overflow */
                for (; j < ev->values_size; j++) ev->values[j].val = &none_value;
            }
            val_destroy(values[vsp].val);
            values[vsp].val = val;
            o_out->val = &none_value;
            values[vsp++].epoint = o_out->epoint;
            continue;
        }

        if (val == &o_SEPARATOR) {
            ev->outp2 = i + 1;
            return 0;
        }
        if (val == &o_COMMA || val == &o_COLON2) continue;
        op2 = val;
        op = op2->u.oper.op;
        if (vsp == 0) goto syntaxe;
        v1 = &values[vsp-1];
        switch (op) {
        case O_FUNC:
        case O_INDEX:
            {
                unsigned int args = 0;
                op = (op == O_FUNC) ? O_PARENT : O_BRACKET;
                while (v1->val->obj != OPER_OBJ || v1->val->u.oper.op != op) {
                    args++;
                    if (vsp <= args) goto syntaxe;
                    v1 = &values[vsp-1-args];
                }
                if (op == O_PARENT) functions(&values[vsp-2-args], args);
                else indexes(&values[vsp-2-args], args);
                vsp -= args + 1;
                continue;
            }
        case O_RBRACKET:
        case O_RPARENT:
        case O_TUPLE:
        case O_LIST:
            {
                unsigned int tup = (op == O_RPARENT), expc = (op == O_TUPLE || op == O_LIST);
                size_t args = 0;
                op = (op == O_RBRACKET || op == O_LIST) ? O_BRACKET : O_PARENT;
                while (v1->val->obj != OPER_OBJ || v1->val->u.oper.op != op) {
                    args++;
                    if (vsp <= args) goto syntaxe;
                    v1 = &values[vsp-1-args];
                }
                if (args == 1) {
                    if (stop && !expc) {
                        vsp--;
                        size_t j = i + 1;
                        if (tup && j < ev->outp && (ev->o_out[j].val->obj != OPER_OBJ || (
                                        ev->o_out[j].val != &o_SEPARATOR && /* (3),2 */
                                        ev->o_out[j].val != &o_RPARENT &&   /* ((3)) */
                                        ev->o_out[j].val != &o_RBRACKET &&  /* [(3)] */
                                        ev->o_out[j].val != &o_FUNC &&      /* f((3)) */
                                        ev->o_out[j].val != &o_LIST &&      /* [(3),] */
                                        ev->o_out[j].val != &o_COMMA &&     /* [(3),(3)] */
                                        ev->o_out[j].val != &o_COMMAY &&    /* (3),y */
                                        ev->o_out[j].val != &o_COMMAZ       /* (3),z */
                                        ))) {
                            v1->val = values[vsp].val; 
                            values[vsp].val = &none_value;
                            continue;
                        }
                        am = (op == O_BRACKET) ? A_LI : A_I;
                        if (values[vsp].val->refcount != 1) {
                            v1->val = val_alloc();
                            apply_addressing(values[vsp].val, v1->val, am);
                        } else {
                            v1->val = values[vsp].val; 
                            values[vsp].val = &none_value;
                            oper.v = apply_addressing(v1->val, v1->val, am);
                            if (oper.v) v1->val = oper.v;
                        }
                        continue;
                    } else if (tup) {
                        vsp--;
                        v1->val = values[vsp].val; 
                        values[vsp].val = &none_value;
                        continue;
                    }
                }
                val = val_alloc();
                val->obj = (op == O_BRACKET) ? LIST_OBJ : TUPLE_OBJ;
                val->u.list.len = args;
                val->u.list.data = list_create_elements(val, args);
                if (args) {
                    while (args--) {
                        v2 = &values[vsp-1];
                        if (v2->val->obj == NONE_OBJ || v2->val->obj == ERROR_OBJ) {
                            val->u.list.data[args] = &none_value;
                            vsp--;
                            while (args--) {
                                val->u.list.data[args] = &none_value;
                                vsp--;
                            }
                            val_destroy(val);
                            val = v2->val;
                            v2->val = &none_value;
                            break;
                        }
                        val->u.list.data[args] = v2->val;
                        v2->val = &none_value;
                        vsp--;
                    }
                }
                v1->val = val;
                continue;
            }
        case O_RBRACE:
        case O_DICT:
            {
                unsigned int args = 0;
                while (v1->val->obj != OPER_OBJ || v1->val->u.oper.op != O_BRACE) {
                    args++;
                    if (vsp <= args) goto syntaxe;
                    v1 = &values[vsp-1-args];
                }
                val = val_realloc(&v1->val);
                val->obj = DICT_OBJ;
                val->u.dict.len = 0;
                val->u.dict.def = NULL;
                avltree_init(&val->u.dict.members);
                if (args) {
                    unsigned int j;
                    vsp -= args;
                    for (j = 0; j < args; j++) {
                        v1 = &values[vsp+j];
                        if (v1->val->obj == NONE_OBJ || v1->val->obj == ERROR_OBJ) {
                            val->obj->destroy(val);
                            v1->val->obj->copy_temp(v1->val, val);
                            break;
                        }
                        if (v1->val->obj == COLONLIST_OBJ) {
                            struct pair_s *p, *p2;
                            struct avltree_node *b;
                            if (v1->val->u.list.data[0]->obj == DEFAULT_OBJ) {
                                if (val->u.dict.def) val_destroy(val->u.dict.def);
                                val->u.dict.def = val_reference(v1->val->u.list.data[1]);
                            } else {
                                p = (struct pair_s *)malloc(sizeof(struct pair_s));
                                if (!p) err_msg_out_of_memory();
                                p->hash = obj_hash(v1->val->u.list.data[0], &new_value, &v1->epoint);
                                if (p->hash >= 0) {
                                    p->key = v1->val->u.list.data[0];
                                    b = avltree_insert(&p->node, &val->u.dict.members, pair_compare);
                                    if (b) {
                                        p2 = avltree_container_of(b, struct pair_s, node);
                                        val_replace(&p2->data, v1->val->u.list.data[1]);
                                        free(p);
                                    } else {
                                        p->key = val_reference(p->key);
                                        p->data = val_reference(v1->val->u.list.data[1]);
                                        val->u.dict.len++;
                                    }
                                } else {
                                    free(p);
                                    val->obj->destroy(val);
                                    new_value.obj->copy_temp(&new_value, val);
                                    break;
                                }
                            }
                            continue;
                        }
                        val->obj->destroy(val);
                        val->obj = ERROR_OBJ;
                        val->u.error.num = ERROR__NOT_KEYVALUE;
                        val->u.error.epoint = v1->epoint;
                        val->u.error.u.objname = v1->val->obj->name;
                        break;
                    }
                }
                continue;
            }
        case O_COND:
            v2 = v1; vsp--;
            if (vsp == 0) goto syntaxe;
            v1 = &values[vsp-1]; vsp--;
            if (vsp == 0) goto syntaxe;
            if (values[vsp-1].val->obj->truth(values[vsp-1].val, &new_value, TRUTH_BOOL, &values[vsp-1].epoint)) {
                val_replace_template(&values[vsp-1].val, &new_value);
                continue;
            }
            if (new_value.u.boolean) {
                struct value_s *tmp = values[vsp-1].val;
                values[vsp-1].val = v1->val;
                v1->val = tmp;
                values[vsp-1].epoint = v1->epoint;
            } else {
                struct value_s *tmp = values[vsp-1].val;
                values[vsp-1].val = v2->val;
                v2->val = tmp;
                values[vsp-1].epoint = v2->epoint;
            }
            continue;
        case O_QUEST:
            vsp--;
            if (vsp == 0) goto syntaxe;
            v1 = &values[vsp-1];
            err_msg2(ERROR______EXPECTED,"':'", &o_out->epoint);
            val_replace(&v1->val, &none_value);
            continue;
        case O_COLON:
            v2 = v1; v1 = &values[--vsp-1];
            if (vsp == 0) goto syntaxe;
            new_value.obj = COLONLIST_OBJ;
            switch (v1->val->obj->type) {
            case T_COLONLIST:
                if (v1->val->refcount == 1) {
                    val = val_alloc();
                    val->obj = COLONLIST_OBJ;
                    if (v2->val->obj == COLONLIST_OBJ && v2->val->refcount == 1) {
                        val->u.list.len = v1->val->u.list.len + v2->val->u.list.len;
                        if (val->u.list.len < v2->val->u.list.len) err_msg_out_of_memory(); /* overflow */
                        val->u.list.data = list_create_elements(val, val->u.list.len);
                        memcpy(val->u.list.data, v1->val->u.list.data, v1->val->u.list.len * sizeof(val->u.list.data[0]));
                        memcpy(val->u.list.data + v1->val->u.list.len, v2->val->u.list.data, v2->val->u.list.len * sizeof(val->u.list.data[0]));
                        v1->val->u.list.len = 0;
                        v2->val->u.list.len = 0;
                        val_destroy(v1->val); v1->val = val;
                        continue;
                    }
                    val->u.list.len = v1->val->u.list.len + 1;
                    if (val->u.list.len < 1) err_msg_out_of_memory(); /* overflow */
                    val->u.list.data = list_create_elements(val, val->u.list.len);
                    memcpy(val->u.list.data, v1->val->u.list.data, v1->val->u.list.len * sizeof(val->u.list.data[0]));
                    val->u.list.data[v1->val->u.list.len] = v2->val;
                    v1->val->u.list.len = 0;
                    v2->val = v1->val;
                    v1->val = val;
                    continue;
                }
                /* fall through */
            default:
                switch (v2->val->obj->type) {
                case T_COLONLIST:
                    if (v2->val->refcount == 1) {
                        val = val_alloc();
                        val->obj = COLONLIST_OBJ;
                        val->u.list.len = v2->val->u.list.len + 1;
                        if (val->u.list.len < 1) err_msg_out_of_memory(); /* overflow */
                        val->u.list.data = list_create_elements(val, val->u.list.len);
                        val->u.list.data[0] = v1->val;
                        memcpy(&val->u.list.data[1], v2->val->u.list.data, v2->val->u.list.len * sizeof(val->u.list.data[0]));
                        v1->val = val;
                        v2->val->u.list.len = 0;
                        continue;
                    }
                    /* fall through */
                default: 
                    val = val_alloc();
                    val->obj = COLONLIST_OBJ;
                    val->u.list.len = 2;
                    val->u.list.data = list_create_elements(val, 2);
                    val->u.list.data[0] = v1->val;
                    val->u.list.data[1] = v2->val;
                    v1->val = val;
                    v2->val = &none_value;
                    continue;
                case T_ERROR:
                case T_NONE:
                    val_replace(&v1->val, v2->val);
                    continue;
                }
            case T_ERROR:
            case T_NONE: continue;
            }
        case O_WORD:    /* <> */
        case O_HWORD:   /* >` */
        case O_BSWORD:  /* >< */
        case O_LOWER:   /* <  */
        case O_HIGHER:  /* >  */
        case O_BANK:    /* `  */
        case O_STRING:  /* ^  */
        case O_INV:     /* ~  */
        case O_NEG:     /* -  */
        case O_POS:     /* +  */
            oper.op = op2;
            oper.v1 = v1->val;
            oper.v2 = NULL;
            oper.epoint = v1->epoint;
            oper.epoint3 = o_out->epoint;
            if (oper.v1->refcount != 1) {
                oper.v = val_alloc();
                oper.v1->obj->calc1(&oper);
                val_destroy(v1->val); v1->val = oper.v;
            } else {
                oper.v = oper.v1;
                oper.v1->obj->calc1(&oper);
            }
            v1->epoint = o_out->epoint;
            continue;
        case O_COMMAS: am = A_SR; goto addr;  /* ,s */
        case O_COMMAR: am = A_RR; goto addr;  /* ,r */
        case O_COMMAZ: am = A_ZR; goto addr;  /* ,z */
        case O_COMMAY: am = A_YR; goto addr;  /* ,y */
        case O_COMMAX: am = A_XR; goto addr;  /* ,x */
        case O_COMMAD: am = A_DR; goto addr;  /* ,d */
        case O_COMMAB: am = A_BR; goto addr;  /* ,b */
        case O_COMMAK: am = A_KR; goto addr;  /* ,k */
        case O_HASH: am = A_IMMEDIATE;        /* #  */
        addr:
            if (v1->val->refcount != 1) {
                oper.v = val_alloc();
                apply_addressing(v1->val, oper.v, am);
                val_destroy(v1->val); v1->val = oper.v;
            } else {
                oper.v = apply_addressing(v1->val, v1->val, am);
                if (oper.v) v1->val = oper.v;
            }
            if (op == O_HASH) v1->epoint = o_out->epoint;
            continue;
        case O_LNOT: /* ! */
            if (v1->val->refcount != 1) {
                oper.v = val_alloc();
                if (!v1->val->obj->truth(v1->val, oper.v, TRUTH_BOOL, &v1->epoint)) oper.v->u.boolean = !oper.v->u.boolean;
                val_destroy(v1->val); v1->val = oper.v;
            } else if (!v1->val->obj->truth(v1->val, v1->val, TRUTH_BOOL, &v1->epoint)) v1->val->u.boolean = !v1->val->u.boolean;
            continue;
        case O_LAND: /* && */
        case O_LOR:  /* || */
        case O_LXOR: /* ^^ */
            v2 = v1; v1 = &values[--vsp-1];
            if (vsp == 0) goto syntaxe;
            if (v1->val->obj->truth(v1->val, &new_value, TRUTH_BOOL, &v1->epoint)) {
                val_replace_template(&v1->val, &new_value);
                continue;
            }
            if (op != O_LXOR) { 
                if (new_value.u.boolean != (op == O_LOR)) {
                    struct value_s *tmp = v1->val;
                    v1->val = v2->val;
                    v2->val = tmp;
                    v1->epoint = v2->epoint;
                }
            } else {
                struct value_s err;
                if (v2->val->obj->truth(v2->val, &err, TRUTH_BOOL, &v2->epoint)) {
                    val_replace_template(&v1->val, &err);
                    continue;
                }
                if (new_value.u.boolean) {
                    if (err.u.boolean) val_replace(&v1->val, &false_value);
                } else {
                    val_replace(&v1->val, err.u.boolean ? v2->val : &false_value);
                }
            }
            continue;
        default: break;
        }
        v2 = v1; v1 = &values[--vsp-1];
        if (vsp == 0) {
        syntaxe:
            err_msg(ERROR_EXPRES_SYNTAX,NULL);
            ev->outp2 = ev->outp;
            return -1;
        }

        oper.op = op2;
        oper.v1 = v1->val;
        oper.v2 = v2->val;
        oper.epoint = v1->epoint;
        oper.epoint2 = v2->epoint;
        oper.epoint3 = o_out->epoint;
        if (oper.v1->refcount == 1 && (oper.v2->refcount != 1 || (oper.v2->obj != LIST_OBJ && oper.v2->obj != TUPLE_OBJ))) {
            oper.v = oper.v1;
            apply_op2(&oper);
        } else if (oper.v2->refcount == 1) {
            v1->val = oper.v2;
            v2->val = oper.v1;
            oper.v = oper.v2;
            apply_op2(&oper);
        } else {
            oper.v = val_alloc();
            apply_op2(&oper);
            val_destroy(v1->val); v1->val = oper.v;
        }
    }
    ev->outp2 = i;
    return 0;
}

/* 0 - normal */
/* 1 - 1 only, till comma */
/* 2 - 1 only, till space  */
/* 3 - opcode */
/* 4 - opcode, with defaults */

int get_exp(int *wd, int stop, struct file_s *cfile) {/* length in bytes, defined */
    char ch;

    struct value_s *op;
    struct values_s o_oper[256];
    uint8_t operp = 0, prec, db;
    struct linepos_s epoint;
    struct value_s *val;
    size_t llen;
    size_t openclose, identlist;

    eval->gstop = stop;
    eval->outp2 = 0;

    if (arguments.tasmcomp) {
        return get_exp_compat(wd, stop);
    }
    eval->outp = 0;
    o_oper[0].val = &o_SEPARATOR;

    *wd=3;    /* 0=byte 1=word 2=long 3=negative/too big */
    openclose=identlist=0;

    ignore();
    switch (here()) {
    case 0:
    case ';': err_msg(ERROR_MISSING_ARGUM,NULL); return 0;
    case '@':
        ch = pline[++lpoint.pos];
        if (!arguments.casesensitive) ch |= 0x20;
        switch (ch) {
        case 'b':*wd=0;break;
        case 'w':*wd=1;break;
        case 'l':*wd=2;break;
        default:err_msg(ERROR______EXPECTED,"@B or @W or @L"); return 0;
        }
        lpoint.pos++;
        break;
    }
    for (;;) {
        ignore(); ch = here(); epoint = lpoint;
        switch (ch) {
        case ',':
            if (stop != 4 || operp) goto tryanon;
            lpoint.pos++;val = push(&epoint);val->obj = DEFAULT_OBJ;
            continue;
        case ')':
            if (operp) {
                const struct value_s *o = o_oper[operp-1].val;
                if (o == &o_COMMA) {operp--;op = &o_TUPLE;goto tphack;}
                else if (o == &o_PARENT || o == &o_FUNC) goto other;
            }
            goto tryanon;
        case ']':
            if (operp) { 
                const struct value_s *o = o_oper[operp-1].val;
                if (o == &o_COMMA) {operp--;op = &o_LIST;goto lshack;}
                else if (o == &o_BRACKET || o == &o_INDEX) goto other;
            }
            goto tryanon;
        case '}':
            if (operp) { 
                const struct value_s *o = o_oper[operp-1].val;
                if (o == &o_COMMA) {operp--;op = &o_DICT;goto brhack;}
                else if (o == &o_BRACE) goto other;
            }
            goto tryanon;
        case ':':
            if (operp) { 
                const struct value_s *o = o_oper[operp-1].val;
                if (o != &o_PARENT && o != &o_BRACKET && o != &o_BRACE && o != &o_FUNC && o != &o_INDEX && o != &o_COMMA) goto tryanon;
            }
            val = push(&epoint);
            val->obj = DEFAULT_OBJ;
            goto other;
        case '(': 
            if ((operp && o_oper[operp-1].val == &o_MEMBER) || identlist) identlist++;
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_PARENT; lpoint.pos++;
            push_oper(&o_PARENT, &epoint);
            openclose++;
            continue;
        case '[':
            if ((operp && o_oper[operp-1].val == &o_MEMBER) || identlist) identlist++;
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_BRACKET; lpoint.pos++;
            push_oper(&o_BRACKET, &epoint);
            openclose++;
            continue;
        case '{':
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_BRACE; lpoint.pos++;
            push_oper(&o_BRACE, &epoint);
            openclose++;
            continue;
        case '+': op = &o_POS; break;
        case '-': op = &o_NEG; break;
        case '!': op = &o_LNOT;break;
        case '~': op = &o_INV; break;
        case '<': if (pline[lpoint.pos+1] == '>') {lpoint.pos++;op = &o_WORD;} else op = &o_LOWER; break;
        case '>': if (pline[lpoint.pos+1] == '`') {lpoint.pos++;op = &o_HWORD;} else if (pline[lpoint.pos+1] == '<') {lpoint.pos++;op = &o_BSWORD;} else op = &o_HIGHER; break;
        case '#': op = &o_HASH; break;
        case '`': op = &o_BANK; break;
        case '^': op = &o_STRING; break;
        case '$': lpoint.pos++;val = push(&epoint);get_hex(val);goto other;
        case '%': if ((pline[lpoint.pos+1] & 0xfe) == 0x30 || (pline[lpoint.pos+1] == '.' && (pline[lpoint.pos+2] & 0xfe) == 0x30)) { lpoint.pos++;val = push(&epoint);get_bin(val);goto other; }
                  goto tryanon;
        case '"':
        case '\'': val = push(&epoint);get_string(val);goto other;
        case '*': lpoint.pos++;val = push(&epoint);get_star(val);goto other;
        case '?': lpoint.pos++;gap_value.refcount++;push_oper(&gap_value, &epoint);goto other;
        case '.': if ((uint8_t)(pline[lpoint.pos+1] ^ 0x30) < 10) goto pushfloat; goto tryanon;
        case 0:
        case ';': 
            if (openclose) {
                printllist(0); /* TODO: conditional */
                if (!mtranslate(cfile)) { /* expand macro parameters, if any */
                    llist = pline;
                    continue;
                }
            }
            /* fall through */
        default: 
            if (ch>='0' && ch<='9') {
            pushfloat:
                val = push(&epoint);
                get_float(val);
                if (val->obj == FLOAT_OBJ && val->u.real == HUGE_VAL) {
                    val->obj = ERROR_OBJ;
                    val->u.error.num = ERROR_CONSTNT_LARGE;
                    val->u.error.epoint = epoint;
                }
                goto other;
            }
            if ((ch | (arguments.casesensitive ? 0 : 0x20)) == 'b' && (pline[lpoint.pos + 1] == '"' || pline[lpoint.pos + 1] == '\'')) {
                lpoint.pos++; val = push(&epoint); get_string(val);
                if (bytes_from_str(val, val)) {
                    val->obj->destroy(val);
                    val->obj = ERROR_OBJ;
                    val->u.error.num = ERROR_BIG_STRING_CO;
                    val->u.error.epoint = epoint;
                }
                goto other;
            }
            if (get_label()) {
                int down;
                struct label_s *l;
                str_t ident;
            as_ident:
                if ((operp && o_oper[operp-1].val == &o_MEMBER) || identlist) {
                    val = push(&epoint);
                    val->obj = IDENT_OBJ;
                    val->u.ident.name.data = pline + epoint.pos;
                    val->u.ident.name.len = lpoint.pos - epoint.pos;
                    val->u.ident.epoint = epoint;
                    goto other;
                } 
                ident.data = pline + epoint.pos;
                ident.len = lpoint.pos - epoint.pos;
                down = (ident.data[0] != '_');
                l = down ? find_label(&ident) : find_label2(&ident, cheap_context);
                if (l) {
                    touch_label(l);
                    l->shadowcheck = 1;
                    push_oper(val_reference(l->value), &epoint);
                    goto other;
                }
                val = push(&epoint);
                val->obj = ERROR_OBJ;
                val->u.error.num = ERROR___NOT_DEFINED;
                val->u.error.epoint = epoint;
                val->u.error.u.notdef.ident = ident;
                val->u.error.u.notdef.label = down ? current_context : cheap_context;
                val->u.error.u.notdef.down = down;
                goto other;
            }
        tryanon:
            db = operp;
            while (operp && o_oper[operp-1].val == &o_POS) operp--;
            if (db != operp) {
                struct label_s *l;
                if ((operp && o_oper[operp-1].val == &o_MEMBER) || identlist) {
                    val = push(&o_oper[operp].epoint);
                    val->obj = ANONIDENT_OBJ;
                    val->u.anonident.count = db - operp -1;
                    val->u.anonident.epoint = o_oper[operp].epoint;
                    goto other;
                }
                l = find_anonlabel(db - operp -1);
                if (l) {
                    touch_label(l);
                    push_oper(val_reference(l->value), &epoint);
                    goto other;
                }
                val = push(&o_oper[operp].epoint);
                val->obj = ERROR_OBJ;
                val->u.error.num = ERROR___NOT_DEFINED;
                val->u.error.epoint = epoint;
                val->u.error.u.notdef.ident.len = 1;
                val->u.error.u.notdef.ident.data = (const uint8_t *)"+";
                val->u.error.u.notdef.label = current_context;
                val->u.error.u.notdef.down = 1;
                goto other;
            }
            while (operp && o_oper[operp-1].val == &o_NEG) operp--;
            if (db != operp) {
                struct label_s *l;
                if ((operp && o_oper[operp-1].val == &o_MEMBER) || identlist) {
                    val = push(&o_oper[operp].epoint);
                    val->obj = ANONIDENT_OBJ;
                    val->u.anonident.count = operp - db;
                    val->u.anonident.epoint = o_oper[operp].epoint;
                    goto other;
                }
                l = find_anonlabel(operp - db);
                if (l) {
                    touch_label(l);
                    push_oper(val_reference(l->value), &epoint);
                    goto other;
                }
                val = push(&o_oper[operp].epoint);
                val->obj = ERROR_OBJ;
                val->u.error.num = ERROR___NOT_DEFINED;
                val->u.error.epoint = epoint;
                val->u.error.u.notdef.ident.len = 1;
                val->u.error.u.notdef.ident.data = (const uint8_t *)"-";
                val->u.error.u.notdef.label = current_context;
                val->u.error.u.notdef.down = 1;
                goto other;
            }
            if (operp && o_oper[operp-1].val == &o_COLON) {
                val = push(&epoint);
                val->obj = DEFAULT_OBJ;
                goto other;
            }
            goto syntaxe;
        }
        lpoint.pos++;
    rtl:
        prec = op->u.oper.prio;
        while (operp && prec < o_oper[operp-1].val->u.oper.prio) {
            operp--;
            push_oper(o_oper[operp].val, &o_oper[operp].epoint);
        }
        o_oper[operp].epoint = epoint;
        o_oper[operp++].val = op;
        continue;
    other:
        if (stop != 2 || openclose) ignore();
        ch = here();epoint = lpoint;
        switch (ch) {
        case ',':
            lpoint.pos++;
            llen = get_label();
            if (llen == 1) {
                uint8_t c = pline[epoint.pos + 1];
                if (!arguments.casesensitive) c |= 0x20;
                switch (c) {
                case 'x': op = &o_COMMAX;llen=0;break;
                case 'y': op = &o_COMMAY;llen=0;break;
                case 'z': op = &o_COMMAZ;llen=0;break;
                case 'r': op = &o_COMMAR;llen=0;break;
                case 's': op = &o_COMMAS;llen=0;break;
                case 'd': op = &o_COMMAD;llen=0;break;
                case 'b': op = &o_COMMAB;llen=0;break;
                case 'k': op = &o_COMMAK;llen=0;break;
                default: op = &o_COMMA;break;
                }
            } else op = &o_COMMA;
            prec = op->u.oper.prio;
            while (operp && prec <= o_oper[operp-1].val->u.oper.prio) {
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            if (op != &o_COMMA) {
                o_oper[operp].epoint = epoint;
                o_oper[operp++].val = op;
                goto other;
            }
            if (!operp) {
                push_oper(&o_SEPARATOR, &epoint);
                if (stop == 1 || stop == 4) {lpoint = epoint;break;}
            } else {
                push_oper(&o_COMMA, &epoint);
                o_oper[operp].epoint = epoint;
                o_oper[operp++].val = op;
            }
            if (llen) {
                epoint.pos++;
                goto as_ident;
            }
            continue;
        case '(':
            prec = o_MEMBER.u.oper.prio;
            while (operp && prec <= o_oper[operp-1].val->u.oper.prio) {
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            push_oper(&o_PARENT, &epoint);
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_FUNC; lpoint.pos++;
            if (identlist) identlist++;
            openclose++;
            continue;
        case '[':
            prec = o_MEMBER.u.oper.prio;
            while (operp && prec <= o_oper[operp-1].val->u.oper.prio) {
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            push_oper(&o_BRACKET, &epoint);
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_INDEX; lpoint.pos++;
            if (identlist) identlist++;
            openclose++;
            continue;
        case '&': if (pline[lpoint.pos+1] == '&') {lpoint.pos+=2;op = &o_LAND;} else {lpoint.pos++;op = &o_AND;} goto push2;
        case '|': if (pline[lpoint.pos+1] == '|') {lpoint.pos+=2;op = &o_LOR;} else {lpoint.pos++;op = &o_OR;} goto push2;
        case '^': if (pline[lpoint.pos+1] == '^') {lpoint.pos+=2;op = &o_LXOR;} else {lpoint.pos++;op = &o_XOR;} goto push2;
        case '*': if (pline[lpoint.pos+1] == '*') {lpoint.pos+=2;op = &o_EXP; goto rtl;} lpoint.pos++;op = &o_MUL; goto push2;
        case '%': lpoint.pos++;op = &o_MOD; goto push2;
        case '/': if (pline[lpoint.pos+1] == '/') {lpoint.pos+=2;op = &o_MOD;} else {lpoint.pos++;op = &o_DIV;} goto push2;
        case '+': lpoint.pos++;op = &o_ADD; goto push2;
        case '-': lpoint.pos++;op = &o_SUB; goto push2;
        case '.': if (pline[lpoint.pos+1] == '.') {lpoint.pos+=2;op = &o_CONCAT;} else {lpoint.pos++;op = &o_MEMBER;} goto push2;
        case '?': lpoint.pos++;op = &o_QUEST; prec = o_COND.u.oper.prio + 1; goto push3;
        case ':': op = &o_COLON;
            prec = op->u.oper.prio + 1;
            while (operp && prec <= o_oper[operp-1].val->u.oper.prio) {
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            if (operp && o_oper[operp-1].val == &o_QUEST) { o_oper[operp-1].val = &o_COND; op = &o_COLON2;}
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = op;
            lpoint.pos++;
            continue;
        case '=': op = &o_EQ; if (pline[lpoint.pos+1] == '=') lpoint.pos+=2; else lpoint.pos++;
        push2:
            prec = op->u.oper.prio;
        push3:
            while (operp && prec <= o_oper[operp-1].val->u.oper.prio) {
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = op;
            continue;
        case '<': 
            switch (pline[lpoint.pos + 1]) {
            case '>': lpoint.pos+=2;op = &o_NE; break;
            case '<': lpoint.pos+=2;op = &o_LSHIFT; break;
            case '=': if (pline[lpoint.pos + 2] == '>') {lpoint.pos += 3; op = &o_CMP;} else {lpoint.pos += 2; op = &o_LE;} break;
            default: lpoint.pos++;op = &o_LT; break;
            }
            goto push2;
        case '>':
            switch (pline[lpoint.pos+1]) {
            case '<': lpoint.pos+=2;op = &o_NE; break;
            case '>': lpoint.pos+=2;op = &o_RSHIFT; break;
            case '=': lpoint.pos+=2;op = &o_GE; break;
            default: lpoint.pos++;op = &o_GT; break;
            }
            goto push2;
        case '!':
            if (pline[lpoint.pos+1]=='=') {lpoint.pos+=2;op = &o_NE;goto push2;}
            goto syntaxe;
        case ')':
            op = &o_RPARENT;
        tphack:
            openclose--;
            if (identlist) identlist--;
            while (operp) {
                const struct value_s *o = o_oper[operp-1].val;
                if (o == &o_PARENT || o == &o_FUNC) break;
                if (o == &o_BRACKET || o == &o_INDEX || o == &o_BRACE) {err_msg(ERROR______EXPECTED,"("); goto error;}
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            lpoint.pos++;
            if (!operp) {err_msg(ERROR______EXPECTED,"("); goto error;}
            operp--;
            push_oper((o_oper[operp].val == &o_PARENT)? op : o_oper[operp].val, &o_oper[operp].epoint);
            goto other;
        case ']':
            op = &o_RBRACKET;
        lshack:
            openclose--;
            if (identlist) identlist--;
            while (operp) {
                const struct value_s *o = o_oper[operp-1].val;
                if (o == &o_BRACKET || o == &o_INDEX) break;
                if (o == &o_PARENT || o == &o_FUNC || o == &o_BRACE) {err_msg(ERROR______EXPECTED,"["); goto error;}
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            lpoint.pos++;
            if (!operp) {err_msg(ERROR______EXPECTED,"["); goto error;}
            operp--;
            push_oper((o_oper[operp].val == &o_BRACKET) ? op : o_oper[operp].val, &o_oper[operp].epoint);
            goto other;
        case '}':
            op = &o_RBRACE;
        brhack:
            openclose--;
            while (operp) {
                const struct value_s *o = o_oper[operp-1].val;
                if (o == &o_BRACE) break;
                if (o == &o_BRACKET || o == &o_INDEX || o == &o_PARENT || o == &o_FUNC) {err_msg(ERROR______EXPECTED,"{"); goto error;}
                operp--;
                push_oper(o_oper[operp].val, &o_oper[operp].epoint);
            }
            lpoint.pos++;
            if (!operp) {err_msg(ERROR______EXPECTED,"{"); goto error;}
            operp--;
            push_oper((o_oper[operp].val == &o_BRACE) ? op : o_oper[operp].val, &o_oper[operp].epoint);
            goto other;
        case 0:
        case ';': 
            if (openclose) {
                printllist(0); /* TODO: conditional */
                if (!mtranslate(cfile)) { /* expand macro parameters, if any */
                    llist = pline;
                    goto other;
                }
            }
            break;
        case '\t':
        case ' ': break;
        default: 
            switch (get_label()) {
            case 1: if (pline[epoint.pos] == 'x') {op = &o_X;goto push2;} break;
            case 2: if (pline[epoint.pos] == 'i' && pline[epoint.pos + 1] == 'n') {op = &o_IN;goto push2;} break;
            }
            goto syntaxe;
        }
        while (operp) {
            const struct value_s *o = o_oper[operp-1].val;
            if (o == &o_PARENT || o == &o_FUNC) {err_msg(ERROR______EXPECTED,")"); goto error;}
            if (o == &o_BRACKET || o == &o_INDEX) {err_msg(ERROR______EXPECTED,"]"); goto error;}
            if (o == &o_BRACE) {err_msg(ERROR______EXPECTED,"}"); goto error;}
            operp--;
            push_oper(o_oper[operp].val, &o_oper[operp].epoint);
        }
        if (!operp) return 1;
    syntaxe:
        err_msg(ERROR_EXPRES_SYNTAX,NULL);
    error:
        return 0;
    }
    return 0;
}

int get_exp_var(struct file_s *cfile) {
    int w;
    return get_exp(&w, 2, cfile);
}

struct value_s *get_vals_tuple(void) {
    size_t ln = 0, i = 0;
    struct value_s **vals = NULL, *retval = NULL, *val;
    struct linepos_s epoint;
    while ((val = get_val(&epoint))) {
        if (i) {
            if (i >= ln) {
                ln += 16;
                vals = (struct value_s **)realloc(vals, ln * sizeof(retval->u.list.data[0]));
                if (!vals || ln < 16 || ln > SIZE_MAX / sizeof(retval->u.list.data[0])) err_msg_out_of_memory();
            }
            if (i == 1) vals[0] = retval;
            vals[i] = val;
        } else retval = val;
        if (val == eval->values->val) eval->values->val = &none_value;
        i++;
    }
    eval_finish();
    if (i > 1) {
        retval = val_alloc();
        retval->obj = TUPLE_OBJ;
        retval->u.list.len = i;
        if (i != ln) {
            vals = (struct value_s **)realloc(vals, i * sizeof(val->u.list.data[0]));
            if (!vals || i > SIZE_MAX / sizeof(val->u.list.data[0])) err_msg_out_of_memory(); /* overflow */
        }
        retval->u.list.data = vals;
    }
    return retval;
}

struct value_s *get_vals_addrlist(struct linepos_s *epoints) {
    size_t ln = 0, i = 0;
    struct value_s **vals = NULL, *retval = NULL, *val;
    struct linepos_s epoint;
    while ((val = get_val((i < 3) ? &epoints[i] : &epoint))) {
        if (i) {
            if (i >= ln) {
                ln += 16;
                vals = (struct value_s **)realloc(vals, ln * sizeof(retval->u.list.data[0]));
                if (!vals || ln < 16 || ln > SIZE_MAX / sizeof(retval->u.list.data[0])) err_msg_out_of_memory();
            }
            if (i == 1) vals[0] = retval;
            vals[i] = val;
        } else retval = val;
        if (val == eval->values->val) eval->values->val = &none_value;
        i++;
    }
    eval_finish();
    if (i > 1) {
        retval = val_alloc();
        retval->obj = ADDRLIST_OBJ;
        retval->u.list.len = i;
        if (i != ln) {
            vals = (struct value_s **)realloc(vals, i * sizeof(val->u.list.data[0]));
            if (!vals || i > SIZE_MAX / sizeof(val->u.list.data[0])) err_msg_out_of_memory(); /* overflow */
        }
        retval->u.list.data = vals;
    }
    return retval ? retval : &null_addrlist;
}

void eval_enter(void) {
    evx_p++;
    if (evx_p >= evxnum) {
        evxnum++;
        evx = (struct eval_context_s **)realloc(evx, evxnum * sizeof(struct eval_context_s *));
        if (!evx || evxnum < 1 || evxnum > SIZE_MAX / sizeof(struct eval_context_s *)) err_msg_out_of_memory(); /* overflow */
        eval = (struct eval_context_s *)malloc(sizeof(struct eval_context_s));
        if (!eval) err_msg_out_of_memory();
        eval->values = NULL;
        eval->values_size = 0;
        eval->o_out = NULL;
        eval->out_size = 0;
        evx[evx_p] = eval;
        return;
    }
    eval = evx[evx_p];
}

void eval_leave(void) {
    if (evx_p) evx_p--;
    eval = evx[evx_p];
}

void init_eval(void) {
    evxnum = 0;
    evx_p = (size_t)-1;
    eval_enter();
}

void destroy_eval(void) {
    while (evxnum--) {
        eval = evx[evxnum];
        while (eval->out_size--) val_destroy(eval->o_out[eval->out_size].val);
        while (eval->values_size--) val_destroy(eval->values[eval->values_size].val);
        free(eval->values);
        free(eval->o_out);
        free(eval);
    }
    free(evx);
}
