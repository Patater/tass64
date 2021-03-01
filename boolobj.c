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
#include "boolobj.h"
#include <string.h>
#include "eval.h"
#include "error.h"
#include "variables.h"
#include "arguments.h"

#include "floatobj.h"
#include "strobj.h"
#include "bytesobj.h"
#include "bitsobj.h"
#include "intobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "errorobj.h"
#include "noneobj.h"
#include "functionobj.h"

static Type obj;

Type *const BOOL_OBJ = &obj;

static Bool trueval = { { &obj, 2 }, NULL };
static Bool falseval = { { &obj, 2 }, NULL };

Bool *true_value = &trueval;
Bool *false_value = &falseval;
Bool *bool_value[2] = { &falseval, &trueval };

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_BOOL: return val_reference(v1);
    default:
        if (v1->obj->truth != DEFAULT_OBJ->truth) {
            return v1->obj->truth(v1, TRUTH_BOOL, epoint);
        }
        break;
    }
    return (Obj *)new_error_conv(v1, BOOL_OBJ, epoint);
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    return o1 == o2;
}

static MUST_CHECK Obj *truth(Obj *o1, Truth_types UNUSED(type), linepos_t UNUSED(epoint)) {
    return val_reference(o1);
}

static MUST_CHECK Error *hash(Obj *o1, int *hs, linepos_t UNUSED(epoint)) {
    *hs = Bool(o1) == true_value ? 1 : 0;
    return NULL;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t UNUSED(epoint), size_t maxsize) {
    Bool *v1 = Bool(o1);
    Str *v;
    bool val = (o1 == &true_value->v);
    size_t len = val ? 4 : 5;
    if (len > maxsize) return NULL;
    v = v1->repr;
    if (v == NULL) {
        v = new_str2(len);
        if (v == NULL) return NULL;
        v->chars = len;
        memcpy(v->data, val ? "true" : "false", len);
        v1->repr = v;
    }
    return val_reference(&v->v);
}

static MUST_CHECK Error *ival(Obj *o1, ival_t *iv, unsigned int UNUSED(bits), linepos_t epoint) {
    if (diagnostics.strict_bool) err_msg_bool(ERROR______CANT_INT, o1, epoint);
    *iv = Bool(o1) == true_value ? 1 : 0;
    return NULL;
}

static MUST_CHECK Error *uval(Obj *o1, uval_t *uv, unsigned int UNUSED(bits), linepos_t epoint) {
    if (diagnostics.strict_bool) err_msg_bool(ERROR______CANT_INT, o1, epoint);
    *uv = Bool(o1) == true_value ? 1 : 0;
    return NULL;
}

MUST_CHECK Float *float_from_bool(const Bool *v1) {
    return new_float(v1 == true_value ? 1.0 : 0.0);
}

MUST_CHECK Obj *int_from_bool(const Bool *v1) {
    return val_reference(&int_value[v1 == true_value ? 1 : 0]->v);
}

static inline MUST_CHECK Obj *int_from_bool2(bool i) {
    return val_reference(&int_value[i ? 1 : 0]->v);
}

static MUST_CHECK Obj *sign(Obj *o1, linepos_t epoint) {
    if (diagnostics.strict_bool) err_msg_bool(ERROR_____CANT_SIGN, o1, epoint);
    return (Obj *)ref_int(int_value[o1 == &true_value->v ? 1 : 0]);
}

static MUST_CHECK Obj *function(oper_t op) {
    if (diagnostics.strict_bool) err_msg_bool((Function(op->v1)->func == F_ABS) ? ERROR______CANT_ABS : ERROR______CANT_INT, op->v2, op->epoint2);
    return (Obj *)ref_int(int_value[op->v2 == &true_value->v ? 1 : 0]);
}

static MUST_CHECK Obj *calc1(oper_t op) {
    bool v1 = Bool(op->v1) == true_value;
    Str *v;
    if (diagnostics.strict_bool && op->op != &o_LNOT) err_msg_bool_oper(op);
    switch (op->op->op) {
    case O_BANK:
    case O_HIGHER:
    case O_LOWER:
    case O_HWORD:
    case O_WORD:
    case O_BSWORD:
        return bytes_calc1(op->op->op, v1 ? 1U : 0U);
    case O_INV: return (Obj *)ibits_from_bool(v1);
    case O_NEG: return v1 ? (Obj *)ibits_from_bool(false) : (Obj *)ref_bits(bits_value[0]);
    case O_POS: return (Obj *)ref_bits(bits_value[v1 ? 1 : 0]);
    case O_STRING:
        v = new_str2(1);
        if (v == NULL) return NULL;
        v->chars = 1;
        v->data[0] = v1 ? '1' : '0';
        return &v->v;
    case O_LNOT: return truth_reference(!v1);
    default: break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *calc2_bool(oper_t op) {
    bool v1 = op->v1 == &true_value->v;
    bool v2 = op->v2 == &true_value->v;
    switch (op->op->op) {
    case O_SUB:
    case O_CMP:
        if (!v1 && v2) return (Obj *)ref_int(minus1_value);
        return int_from_bool2(v1 != v2);
    case O_EQ: return truth_reference(v1 == v2);
    case O_NE: return truth_reference(v1 != v2);
    case O_MIN:
    case O_LT: return truth_reference(!v1 && v2);
    case O_LE: return truth_reference(!v1 || v2);
    case O_MAX:
    case O_GT: return truth_reference(v1 && !v2);
    case O_GE: return truth_reference(v1 || !v2);
    case O_ADD: return (Obj *)int_from_uval((v1 ? 1U : 0U) + (v2 ? 1U : 0U));
    case O_MUL: return int_from_bool2(v1 && v2);
    case O_DIV:
        if (!v2) {
            return (Obj *)new_error_obj(ERROR_DIVISION_BY_Z, op->v2, op->epoint3);
        }
        return int_from_bool2(v1);
    case O_MOD:
        if (!v2) {
            return (Obj *)new_error_obj(ERROR_DIVISION_BY_Z, op->v2, op->epoint3);
        }
        return int_from_bool2(false);
    case O_EXP: return int_from_bool2(v1 || !v2);
    case O_AND: return truth_reference(v1 && v2);
    case O_OR: return truth_reference(v1 || v2);
    case O_XOR: return truth_reference(v1 != v2);
    case O_LSHIFT: return v2 ? (Obj *)bits_from_bools(v1, false) : (Obj *)ref_bits(bits_value[v1 ? 1 : 0]);
    case O_RSHIFT: return v2 ? (Obj *)ref_bits(null_bits) : (Obj *)ref_bits(bits_value[v1 ? 1 : 0]);
    case O_CONCAT: return (Obj *)bits_from_bools(v1, v2);
    default: break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *o1 = op->v1;
    Obj *o2 = op->v2;
    bool is_bool = o2->obj == BOOL_OBJ;
    Oper *oper = op->op;

    if (oper == &o_LAND) {
        if (diagnostics.strict_bool && !is_bool) err_msg_bool_oper(op);
        return val_reference(o1 == &true_value->v ? o2 : o1);
    }
    if (oper == &o_LOR) {
        if (diagnostics.strict_bool && !is_bool) err_msg_bool_oper(op);
        return val_reference(o1 == &true_value->v ? o1 : o2);
    }
    if (is_bool) {
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        return calc2_bool(op);
    }
    if (oper != &o_MEMBER && oper != &o_X) {
        return o2->obj->rcalc2(op);
    }
    if (o2 == &none_value->v || o2->obj == ERROR_OBJ) return val_reference(o2);
    return obj_oper_error(op);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    Obj *o1 = op->v1;
    if (o1->obj == BOOL_OBJ) {
        return calc2_bool(op);
    }
    if (op->op != &o_IN) {
        return o1->obj->calc2(op);
    }
    return obj_oper_error(op);
}

void boolobj_init(void) {
    new_type(&obj, T_BOOL, "bool", sizeof(Bool));
    obj.create = create;
    obj.same = same;
    obj.truth = truth;
    obj.hash = hash;
    obj.repr = repr;
    obj.ival = ival;
    obj.uval = uval;
    obj.uval2 = uval;
    obj.iaddress = ival;
    obj.uaddress = uval;
    obj.sign = sign;
    obj.function = function;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
}

void boolobj_names(void) {
    new_builtin("bool", val_reference(&BOOL_OBJ->v));

    new_builtin("true", &true_value->v);
    new_builtin("false", &false_value->v);
}

void boolobj_destroy(void) {
#ifdef DEBUG
    if (false_value->v.refcount != 1) fprintf(stderr, "false %" PRIuSIZE "\n", false_value->v.refcount - 1);
    if (true_value->v.refcount != 1) fprintf(stderr, "true %" PRIuSIZE "\n", true_value->v.refcount - 1);
    if (falseval.repr != NULL && falseval.repr->v.refcount != 1) fprintf(stderr, "boolrepr[0] %" PRIuSIZE "\n", falseval.repr->v.refcount - 1);
    if (trueval.repr != NULL && trueval.repr->v.refcount != 1) fprintf(stderr, "boolrepr[1] %" PRIuSIZE "\n", trueval.repr->v.refcount - 1);
#endif

    if (falseval.repr != NULL) val_destroy(&falseval.repr->v);
    if (trueval.repr != NULL) val_destroy(&trueval.repr->v);
}
