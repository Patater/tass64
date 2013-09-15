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
#include <string.h>
#include "values.h"
#include "addressobj.h"

#include "boolobj.h"
#include "intobj.h"
#include "strobj.h"

static struct obj_s obj;

obj_t ADDRESS_OBJ = &obj;

static void destroy(struct value_s *v1) {
    val_destroy(v1->u.addr.val);
}

static void copy(const struct value_s *v1, struct value_s *v) {
    v->obj = ADDRESS_OBJ;
    v->refcount = 1;
    v->u.addr.type = v1->u.addr.type;
    v->u.addr.val = val_reference(v1->u.addr.val);
}

static void copy_temp(const struct value_s *v1, struct value_s *v) {
    v->obj = ADDRESS_OBJ;
    v->refcount = 1;
    v->u.addr.type = v1->u.addr.type;
    v->u.addr.val = v1->u.addr.val;
}

static int same(const struct value_s *v1, const struct value_s *v2) {
    return v2->obj == ADDRESS_OBJ && v1->u.addr.type == v2->u.addr.type && obj_same(v1->u.addr.val, v2->u.addr.val);
}

static int truth(const struct value_s *v1) {
    return v1->u.addr.val->obj->truth(v1->u.addr.val);
}

static int MUST_CHECK ival(const struct value_s *v1, struct value_s *v, ival_t *iv, int bits, linepos_t epoint) {
    if (v1->u.addr.type != A_NONE) {
        v->obj = ERROR_OBJ;
        v->u.error.num = ERROR_____CANT_IVAL;
        v->u.error.u.bits = bits;
        v->u.error.epoint = *epoint;
        return 1;
    }
    return v1->u.addr.val->obj->ival(v1->u.addr.val, v, iv, bits, epoint);
}

static int MUST_CHECK uval(const struct value_s *v1, struct value_s *v, uval_t *uv, int bits, linepos_t epoint) {
    if (v1->u.addr.type != A_NONE) {
        v->obj = ERROR_OBJ;
        v->u.error.num = ERROR_____CANT_UVAL;
        v->u.error.u.bits = bits;
        v->u.error.epoint = *epoint;
        return 1;
    }
    return v1->u.addr.val->obj->uval(v1->u.addr.val, v, uv, bits, epoint);
}

static int MUST_CHECK real(const struct value_s *v1, struct value_s *v, double *r, linepos_t epoint) {
    if (v1->u.addr.type != A_NONE) {
        v->obj = ERROR_OBJ;
        v->u.error.num = ERROR_____CANT_REAL;
        v->u.error.epoint = *epoint;
        return 1;
    }
    return v1->u.addr.val->obj->real(v1->u.addr.val, v, r, epoint);
}

static int MUST_CHECK sign(const struct value_s *v1, struct value_s *v, int *s, linepos_t epoint) {
    if (v1->u.addr.type != A_NONE) {
        v->obj = ERROR_OBJ;
        v->u.error.num = ERROR_____CANT_SIGN;
        v->u.error.epoint = *epoint;
        return 1;
    }
    return v1->u.addr.val->obj->sign(v1->u.addr.val, v, s, epoint);
}

static void absolute(const struct value_s *v1, struct value_s *v, linepos_t epoint) {
    if (v1->u.addr.type != A_NONE) {
        v->obj = ERROR_OBJ;
        v->u.error.num = ERROR______CANT_ABS;
        v->u.error.epoint = *epoint;
        return;
    }
    return v1->u.addr.val->obj->abs(v1->u.addr.val, v, epoint);
}

static void integer(const struct value_s *v1, struct value_s *v, linepos_t epoint) {
    if (v1->u.addr.type != A_NONE) {
        v->obj = ERROR_OBJ;
        v->u.error.num = ERROR______CANT_ABS;
        v->u.error.epoint = *epoint;
        return;
    }
    return v1->u.addr.val->obj->integer(v1->u.addr.val, v, epoint);
}

static void repr(const struct value_s *v1, struct value_s *v) {
    uint8_t *s;
    size_t len;
    char buffer[100], buffer2[100];
    atype_t addrtype;
    int ind, ind2;
    struct value_s tmp;

    v1->u.addr.val->obj->repr(v1->u.addr.val, &tmp);
    if (tmp.obj != STR_OBJ) {
        if (v == v1) v->obj->destroy(v);
        tmp.obj->copy_temp(&tmp, v);
        return;
    }
    ind2 = 0;
    addrtype = v1->u.addr.type;
    ind = 99;
    buffer2[ind] = '\0';
    while (addrtype & 0xfff) {
        switch ((enum atype_e)((addrtype & 0xf00) >> 8)) {
        case A_NONE:break;
        case A_XR: buffer[ind2++] = ','; buffer[ind2++] = 'x';break;
        case A_YR: buffer[ind2++] = ','; buffer[ind2++] = 'y';break;
        case A_ZR: buffer[ind2++] = ','; buffer[ind2++] = 'z';break;
        case A_SR: buffer[ind2++] = ','; buffer[ind2++] = 's';break;
        case A_RR: buffer[ind2++] = ','; buffer[ind2++] = 'r';break;
        case A_DR: buffer[ind2++] = ','; buffer[ind2++] = 'd';break;
        case A_BR: buffer[ind2++] = ','; buffer[ind2++] = 'b';break;
        case A_KR: buffer[ind2++] = ','; buffer[ind2++] = 'k';break;
        case A_I: buffer2[--ind] = '('; buffer[ind2++] = ')';break;
        case A_LI: buffer2[--ind] = '['; buffer[ind2++] = ']';break;
        case A_IMMEDIATE: buffer2[--ind] = '#';break;
        }
        addrtype <<= 4;
    }

    len = 99 - ind + ind2;

    if (v == v1) v->obj->destroy(v);
    v->obj = STR_OBJ;
    v->u.str.len = len + tmp.u.str.len;
    v->u.str.chars = len + tmp.u.str.chars;
    s = (uint8_t *)malloc(v->u.str.len);
    if (!s || v->u.str.len < len) err_msg_out_of_memory(); /* overflow */
    memcpy(s, buffer2 + ind, 99 - ind);
    memcpy(s + 99 - ind, tmp.u.str.data, tmp.u.str.len);
    memcpy(s + 99 - ind + tmp.u.str.len, buffer, ind2);
    v->u.str.data = s;
    tmp.obj->destroy(&tmp);
}

static inline int check_addr(atype_t type) {
    while (type) {
        switch ((enum atype_e)(type & 15)) {
        case A_I:
        case A_LI:
        case A_IMMEDIATE: return 1;
        case A_KR:
        case A_DR:
        case A_BR:
        case A_XR:
        case A_YR:
        case A_ZR:
        case A_RR:
        case A_SR:
        case A_NONE: break;
        }
        type >>= 4;
    }
    return 0;
}

static void calc1(oper_t op) {
    struct value_s *v = op->v, *v1 = op->v1, *v2;
    atype_t am;
    switch (op->op->u.oper.op) {
    case O_BANK:
    case O_HIGHER:
    case O_LOWER:
    case O_HWORD:
    case O_WORD:
    case O_BSWORD:
    case O_INV:
    case O_NEG:
    case O_POS:
        am = v1->u.addr.type;
        if (am != A_IMMEDIATE && am != A_NONE) break;
        op->v1 = v1->u.addr.val;
        op->v = val_alloc();
        op->v1->obj->calc1(op);
        v->obj = ADDRESS_OBJ; 
        v->u.addr.val = op->v;
        v->u.addr.type = am;
        op->v1 = v1;
        op->v = v;
        return;
    case O_COMMAS: am = A_SR; goto addr;
    case O_COMMAR: am = A_RR; goto addr;
    case O_COMMAZ: am = A_ZR; goto addr;
    case O_COMMAY: am = A_YR; goto addr;
    case O_COMMAX: am = A_XR; goto addr;
    case O_COMMAD: am = A_DR; goto addr;
    case O_COMMAB: am = A_BR; goto addr;
    case O_COMMAK: am = A_KR; goto addr;
    case O_HASH: am = A_IMMEDIATE;
    addr:
        am |= v1->u.addr.type << 4;
        if (v == v1) {
            v2 = val_alloc();
            v1->u.addr.val->obj->copy(v1->u.addr.val, v2);
            destroy(v);
        } else v2 = val_reference(v1->u.addr.val);
        v->obj = ADDRESS_OBJ; 
        v->u.addr.val = v2;
        v->u.addr.type = am;
        return;
    case O_LNOT: bool_from_int(v, !truth(v1)); return;
    case O_STRING: break;
    default: break;
    }
    obj_oper_error(op);
}

static void calc2(oper_t op) {
    struct value_s *v1 = op->v1, *v2 = op->v2, *v = op->v;
    atype_t am;
    switch (v2->obj->type) {
    case T_ADDRESS:
        am = v1->u.addr.type;
        if (am != A_IMMEDIATE || v2->u.addr.type != A_IMMEDIATE) 
            if (am != A_NONE || v2->u.addr.type != A_NONE) 
                break;
        op->v = val_alloc();
        op->v1 = v1->u.addr.val;
        op->v2 = v2->u.addr.val;
        op->v1->obj->calc2(op);
        if (v == v1 || v == v2) destroy(v);
        v->obj = ADDRESS_OBJ;
        v->u.addr.val = op->v;
        v->u.addr.type = am;
        op->v = v;
        op->v1 = v1;
        op->v2 = v2;
        return;
    case T_BOOL:
    case T_INT:
    case T_BITS:
    case T_FLOAT: 
    case T_CODE: 
        am = v1->u.addr.type;
        if (check_addr(am)) break;
        op->v = val_alloc();
        op->v1 = v1->u.addr.val;
        op->v1->obj->calc2(op);
        if (v == v1 || v == v2) v->obj->destroy(v);
        v->obj = ADDRESS_OBJ;
        v->u.addr.val = op->v;
        v->u.addr.type = am;
        op->v = v;
        op->v1 = v1;
        return;
    default: 
        if (op->op != &o_MEMBER) {
            v2->obj->rcalc2(op); return;
        }
    }
    obj_oper_error(op);
}

static void rcalc2(oper_t op) {
    struct value_s *v1 = op->v1, *v2 = op->v2, *v = op->v;
    atype_t am;
    switch (v1->obj->type) {
    case T_ADDRESS:
        am = v2->u.addr.type;
        if (v1->u.addr.type != A_IMMEDIATE || am != A_IMMEDIATE) 
            if (v1->u.addr.type != A_NONE || am != A_NONE) 
                break;
        op->v = val_alloc();
        op->v1 = v1->u.addr.val;
        op->v2 = v2->u.addr.val;
        op->v2->obj->rcalc2(op);
        if (v == v1 || v == v2) destroy(v);
        v->obj = ADDRESS_OBJ;
        v->u.addr.val = op->v;
        v->u.addr.type = am;
        op->v = v;
        op->v1 = v1;
        op->v2 = v2;
        return;
    case T_BOOL:
    case T_INT:
    case T_BITS:
    case T_FLOAT: 
    case T_CODE: 
        am = v2->u.addr.type;
        if (check_addr(am)) break;
        op->v = val_alloc();
        op->v2 = v2->u.addr.val;
        op->v2->obj->rcalc2(op);
        if (v == v1 || v == v2) v->obj->destroy(v);
        v->obj = ADDRESS_OBJ;
        v->u.addr.val = op->v;
        v->u.addr.type = am;
        op->v = v;
        op->v2 = v2;
        return;
    default:
        if (op->op != &o_IN) {
            v1->obj->calc2(op); return;
        }
    }
    obj_oper_error(op);
}

void addressobj_init(void) {
    obj_init(&obj, T_ADDRESS, "<address>");
    obj.destroy = destroy;
    obj.copy = copy;
    obj.copy_temp = copy_temp;
    obj.same = same;
    obj.truth = truth;
    obj.repr = repr;
    obj.ival = ival;
    obj.uval = uval;
    obj.real = real;
    obj.sign = sign;
    obj.abs = absolute;
    obj.integer = integer;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
}
