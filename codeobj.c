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
#include "codeobj.h"
#include <string.h>
#include "eval.h"
#include "mem.h"
#include "64tass.h"
#include "section.h"
#include "variables.h"
#include "error.h"
#include "values.h"
#include "arguments.h"

#include "boolobj.h"
#include "floatobj.h"
#include "namespaceobj.h"
#include "listobj.h"
#include "intobj.h"
#include "bitsobj.h"
#include "bytesobj.h"
#include "operobj.h"
#include "gapobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"
#include "memblocksobj.h"
#include "symbolobj.h"
#include "addressobj.h"

static Type obj;

Type *const CODE_OBJ = &obj;

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_CODE: return val_reference(v1);
    default: break;
    }
    return new_error_conv(v1, CODE_OBJ, epoint);
}

static FAST_CALL void destroy(Obj *o1) {
    Code *v1 = Code(o1);
    val_destroy(v1->typ);
    val_destroy(Obj(v1->memblocks));
    val_destroy(Obj(v1->names));
}

static FAST_CALL void garbage(Obj *o1, int i) {
    Code *v1 = Code(o1);
    Obj *v;
    switch (i) {
    case -1:
        v1->typ->refcount--;
        v1->memblocks->v.refcount--;
        v1->names->v.refcount--;
        return;
    case 0:
        return;
    case 1:
        v = v1->typ;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        v = Obj(v1->memblocks);
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        v = Obj(v1->names);
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

static MUST_CHECK Error *access_check(const Code *v1, linepos_t epoint) {
    if ((v1->requires & ~current_section->provides) != 0) {
        return new_error(ERROR_REQUIREMENTS_, epoint);
    }
    if ((v1->conflicts & current_section->provides) != 0) {
        return new_error(ERROR______CONFLICT, epoint);
    }
    return NULL;
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Code *v1 = Code(o1), *v2 = Code(o2);
    return o1->obj == o2->obj && v1->addr == v2->addr && (v1->typ == v2->typ || v1->typ->obj->same(v1->typ, v2->typ))
        && v1->size == v2->size && v1->offs == v2->offs && v1->dtype == v2->dtype
        && v1->requires == v2->requires && v1->conflicts == v2->conflicts
/*        && (v1->memblocks == v2->memblocks || v1->memblocks->v.obj->same(Obj(v1->memblocks), Obj(v2->memblocks))) */
        && (v1->names == v2->names || v1->names->v.obj->same(Obj(v1->names), Obj(v2->names)));
}

static inline address_t code_address(const Code *v1) {
    return v1->offs < 0 ? v1->addr - (uval_t)-v1->offs : v1->addr + (uval_t)v1->offs;
}

static MUST_CHECK Obj *get_code_address(const Code *v1, linepos_t epoint) {
    address_t addr2 = code_address(v1);
    address_t addr = addr2 & all_mem;
    if (addr2 != addr) err_msg_addr_wrap(epoint);
    return get_star_value(addr, v1->typ);
}

MUST_CHECK Obj *get_code_value(const Code *v1, linepos_t epoint) {
    Error *err = access_check(v1, epoint);
    if (err != NULL) return Obj(err);
    return get_code_address(v1, epoint);
}

MUST_CHECK Error *code_uaddress(Obj *o1, uval_t *uv, uval_t *uv2, linepos_t epoint) {
    unsigned int bits = all_mem_bits;
    Code *v1 = Code(o1);
    Error *v = access_check(v1, epoint);
    if (v != NULL) return v;
    *uv = code_address(v1);
    *uv2 = v1->addr;
    if (bits >= sizeof(*uv2)*8 || (*uv2 >> bits) == 0) {
        return NULL;
    }
    v = new_error(ERROR_____CANT_UVAL, epoint);
    v->u.intconv.bits = bits;
    v->u.intconv.val = get_star_value(*uv & all_mem, v1->typ);
    return v;
}

static MUST_CHECK Obj *truth(Obj *o1, Truth_types type, linepos_t epoint) {
    Code *v1 = Code(o1);
    Obj *v, *result;
    Error *err;
    err = access_check(v1, epoint);
    if (err != NULL) return Obj(err);
    v = get_code_address(v1, epoint);
    result = v->obj->truth(v, type, epoint);
    val_destroy(v);
    return result;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    Code *v1 = Code(o1);
    Obj *v, *result;
    if (epoint != NULL) {
        Error *err = access_check(v1, epoint);
        if (err != NULL) return Obj(err);
        v = get_code_address(v1, epoint);
    } else v = get_star_value(code_address(v1) & all_mem, v1->typ);
    result = v->obj->repr(v, epoint, maxsize);
    val_destroy(v);
    return result;
}

static MUST_CHECK Obj *str(Obj *o1, linepos_t epoint, size_t maxsize) {
    Code *v1 = Code(o1);
    Obj *v, *result;
    if (epoint != NULL) {
        Error *err = access_check(v1, epoint);
        if (err != NULL) return Obj(err);
        v = get_code_address(v1, epoint);
    } else v = get_star_value(code_address(v1) & all_mem, v1->typ);
    result = v->obj->str(v, epoint, maxsize);
    val_destroy(v);
    return result;
}

static MUST_CHECK Error *iaddress(Obj *o1, ival_t *iv, unsigned int bits, linepos_t epoint) {
    Code *v1 = Code(o1);
    address_t addr, addr2;
    Error *v = access_check(v1, epoint);
    if (v != NULL) return v;
    addr2 = code_address(v1);
    addr = addr2 & all_mem;
    *iv = (ival_t)addr;
    if ((addr >> (bits - 1)) == 0) {
        if (addr2 != addr) err_msg_addr_wrap(epoint);
        return NULL;
    }
    v = new_error(ERROR_____CANT_IVAL, epoint);
    v->u.intconv.bits = bits;
    v->u.intconv.val = get_star_value(addr, v1->typ);
    return v;
}

static MUST_CHECK Error *uaddress(Obj *o1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Code *v1 = Code(o1);
    address_t addr, addr2;
    Error *v = access_check(v1, epoint);
    if (v != NULL) return v;
    addr2 = code_address(v1);
    addr = addr2 & all_mem;
    *uv = addr;
    if (bits >= sizeof(addr)*8 || (addr >> bits) == 0) {
        if (addr2 != addr) err_msg_addr_wrap(epoint);
        return NULL;
    }
    v = new_error(ERROR_____CANT_UVAL, epoint);
    v->u.intconv.bits = bits;
    v->u.intconv.val = get_star_value(addr, v1->typ);
    return v;
}

static MUST_CHECK Error *ival(Obj *o1, ival_t *iv, unsigned int bits, linepos_t epoint) {
    Code *v1 = Code(o1);
    if (v1->typ->obj->address(v1->typ) != A_NONE) {
        Error *v = new_error(ERROR______CANT_INT, epoint);
        v->u.obj = get_star_value(code_address(v1) & all_mem, v1->typ);
        return v;
    }
    return iaddress(o1, iv, bits, epoint);
}

static MUST_CHECK Error *uval(Obj *o1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Code *v1 = Code(o1);
    if (v1->typ->obj->address(v1->typ) != A_NONE) {
        Error *v = new_error(ERROR______CANT_INT, epoint);
        v->u.obj = get_star_value(code_address(v1) & all_mem, v1->typ);
        return v;
    }
    return uaddress(o1, uv, bits, epoint);
}

static FAST_CALL uint32_t address(const Obj *o1) {
    Code *v1 = Code(o1);
    Obj *v = v1->typ;
    return v->obj->address(v);
}

MUST_CHECK Obj *float_from_code(const Code *v1, linepos_t epoint) {
    Obj *v, *result;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return Obj(err);
    v = get_code_address(v1, epoint);
    result = FLOAT_OBJ->create(v, epoint);
    val_destroy(v);
    return result;
}

static MUST_CHECK Obj *sign(Obj *o1, linepos_t epoint) {
    Code *v1 = Code(o1);
    Obj *v, *result;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return Obj(err);
    v = get_code_address(v1, epoint);
    result = v->obj->sign(v, epoint);
    val_destroy(v);
    return result;
}

static MUST_CHECK Obj *function(oper_t op) {
    Code *v1 = Code(op->v2);
    Obj *v, *result;
    Error *err = access_check(v1, op->epoint2);
    if (err != NULL) return Obj(err);
    op->v2 = v = get_code_address(v1, op->epoint2);
    op->inplace = v->refcount == 1 ? v : NULL;
    result = v->obj->function(op);
    val_destroy(v);
    return result;
}

MUST_CHECK Obj *int_from_code(const Code *v1, linepos_t epoint) {
    Obj *v, *result;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return Obj(err);
    v = get_code_address(v1, epoint);
    result = INT_OBJ->create(v, epoint);
    val_destroy(v);
    return result;
}

static address_t calc_size(const Code *v1) {
    if (v1->offs >= 0) {
        if (v1->size < (uval_t)v1->offs) return 0;
        return v1->size - (uval_t)v1->offs;
    }
    if (v1->size + (uval_t)-v1->offs < v1->size) err_msg_out_of_memory(); /* overflow */
    return v1->size + (uval_t)-v1->offs;
}

static MUST_CHECK Obj *len(oper_t op) {
    address_t ln, s;
    Code *v1 = Code(op->v2);
    if (v1->pass == 0) {
        return Obj(ref_none());
    }
    if (v1->offs == 0) {
        s = v1->size;
    } else if (v1->offs > 0) {
        s = v1->size - (uval_t)v1->offs;
        if (s > v1->size) return Obj(new_error(ERROR_NEGATIVE_SIZE, op->epoint2));
    } else {
        s = v1->size + (uval_t)-v1->offs;
        if (s < v1->size) err_msg_out_of_memory(); /* overflow */
        if (diagnostics.size_larger) err_msg_size_larger(op->epoint2);
    }
    ln = (v1->dtype < 0) ? (address_t)-v1->dtype : (address_t)v1->dtype;
    return Obj(int_from_size((ln != 0) ? (s / ln) : s));
}

static MUST_CHECK Obj *size(oper_t op) {
    address_t s;
    Code *v1 = Code(op->v2);
    if (v1->pass == 0) {
        return Obj(ref_none());
    }
    if (v1->offs == 0) {
        s = v1->size;
    } else if (v1->offs > 0) {
        s = v1->size - (uval_t)v1->offs;
        if (s > v1->size) return Obj(new_error(ERROR_NEGATIVE_SIZE, op->epoint2));
    } else {
        s = v1->size + (uval_t)-v1->offs;
        if (s < v1->size) err_msg_out_of_memory(); /* overflow */
        if (diagnostics.size_larger) err_msg_size_larger(op->epoint2);
    }
    return Obj(int_from_size(s));
}

MUST_CHECK Obj *bits_from_code(const Code *v1, linepos_t epoint) {
    Obj *v, *result;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return Obj(err);
    v = get_code_address(v1, epoint);
    result = BITS_OBJ->create(v, epoint);
    val_destroy(v);
    return result;
}

MUST_CHECK Obj *bytes_from_code(const Code *v1, linepos_t epoint) {
    Obj *v, *result;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return Obj(err);
    v = get_code_address(v1, epoint);
    result = BYTES_OBJ->create(v, epoint);
    val_destroy(v);
    return result;
}

static MUST_CHECK Obj *code_item(const Code *v1, ssize_t offs2, size_t ln2) {
    int r;
    size_t i2, offs;
    uval_t val;
    if (offs2 < 0) return Obj(ref_gap());
    offs = (size_t)offs2 * ln2;
    r = -1;
    for (val = i2 = 0; i2 < ln2; i2++, offs++) {
        r = read_mem(v1->memblocks, v1->memaddr, v1->membp, offs);
        if (r < 0) return Obj(ref_gap());
        val |= (uval_t)r << (i2 * 8);
    }
    if (v1->dtype < 0 && (r & 0x80) != 0) {
        for (; i2 < sizeof val; i2++) val |= (uval_t)0xff << (i2 * 8);
    }
    return (v1->dtype < 0) ? Obj(int_from_ival((ival_t)val)) : Obj(int_from_uval(val));
}

MUST_CHECK Obj *tuple_from_code(const Code *v1, const Type *typ) {
    address_t ln, ln2;
    size_t  i;
    ssize_t offs;
    List *v;
    Obj **vals;

    ln2 = (v1->dtype < 0) ? (address_t)-v1->dtype : (address_t)v1->dtype;
    if (ln2 == 0) ln2 = 1;
    ln = calc_size(v1) / ln2;

    if (ln == 0) {
        return val_reference(typ == TUPLE_OBJ ? Obj(null_tuple) : Obj(null_list));
    }

    v = List(val_alloc(typ));
    v->len = ln;
    v->data = vals = list_create_elements(v, ln);
    if (v1->offs >= 0) {
        offs = (ssize_t)(((uval_t)v1->offs + ln2 - 1) / ln2);
    } else {
        offs = -(ssize_t)(((uval_t)-v1->offs + ln2 - 1) / ln2);
    }
    for (i = 0; i < ln; i++, offs++) {
        vals[i] = code_item(v1, offs, ln2);
    }
    return Obj(v);
}

static MUST_CHECK Obj *slice(oper_t op, size_t indx) {
    Obj **vals;
    size_t i;
    address_t ln, ln2;
    size_t offs1;
    ssize_t offs0;
    Code *v1 = Code(op->v1);
    Obj *o2 = op->v2;
    Obj *err;
    Funcargs *args = Funcargs(o2);
    linepos_t epoint2;

    if (args->len < 1 || args->len > indx + 1) {
        return new_error_argnum(args->len, 1, indx + 1, op->epoint2);
    }
    o2 = args->val[indx].val;
    epoint2 = &args->val[indx].epoint;

    ln2 = (v1->dtype < 0) ? (address_t)-v1->dtype : (address_t)v1->dtype;
    if (ln2 == 0) ln2 = 1;
    ln = calc_size(v1) / ln2;

    if (v1->offs >= 0) {
        offs0 = (ssize_t)(((uval_t)v1->offs + ln2 - 1) / ln2);
    } else {
        offs0 = -(ssize_t)(((uval_t)-v1->offs + ln2 - 1) / ln2);
    }
    if (o2->obj->iterable) {
        struct iter_s iter;
        Tuple *v;
        iter.data = o2; o2->obj->getiter(&iter);

        if (iter.len == 0) {
            iter_destroy(&iter);
            return Obj(ref_tuple(null_tuple));
        }
        v = new_tuple(iter.len);
        vals = v->data;
        for (i = 0; i < iter.len && (o2 = iter.next(&iter)) != NULL; i++) {
            err = indexoffs(o2, ln, &offs1, epoint2);
            if (err != NULL) {
                vals[i] = err;
                continue;
            }
            vals[i] = code_item(v1, (ssize_t)offs1 + offs0, ln2);
        }
        iter_destroy(&iter);
        v->len = i;
        return Obj(v);
    }
    if (o2->obj == COLONLIST_OBJ) {
        struct sliceparam_s s;
        Tuple *v;

        err = sliceparams(Colonlist(o2), ln, &s, epoint2);
        if (err != NULL) return err;

        if (s.length == 0) {
            return Obj(ref_tuple(null_tuple));
        }
        v = new_tuple(s.length);
        vals = v->data;
        for (i = 0; i < s.length; i++) {
            vals[i] = code_item(v1, s.offset + offs0, ln2);
            s.offset += s.step;
        }
        return Obj(v);
    }
    err = indexoffs(o2, ln, &offs1, epoint2);
    if (err != NULL) return err;

    return code_item(v1, (ssize_t)offs1 + offs0, ln2);
}

static inline address_t ldigit(Code *v1, linepos_t epoint) {
    address_t addr2 = code_address(v1);
    address_t addr = addr2 & all_mem;
    if (addr2 != addr) err_msg_addr_wrap(epoint);
    return addr;
}

static MUST_CHECK Obj *calc1(oper_t op) {
    Obj *v, *result;
    Error *err;
    Code *v1 = Code(op->v1);
    switch (op->op->op) {
    case O_LNOT:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        v = get_code_address(v1, op->epoint);
        op->v1 = v;
        op->inplace = (op->inplace == Obj(v1) && v->refcount == 1) ? v : NULL;
        result = op->v1->obj->calc1(op);
        val_destroy(v);
        return result;
    case O_BANK:
        if (all_mem < 0xffffff) return bytes_calc1(op->op->op, ldigit(v1, op->epoint));
        /* fall through */
    case O_HIGHER:
    case O_LOWER:
    case O_HWORD:
    case O_WORD:
    case O_BSWORD:
        return bytes_calc1(op->op->op, code_address(v1) & all_mem);
    case O_STRING:
    case O_INV:
    case O_NEG:
    case O_POS:
        err = access_check(v1, op->epoint);
        if (err != NULL) return Obj(err);
        v = get_code_address(v1, op->epoint);
        op->v1 = v;
        op->inplace = (op->inplace == Obj(v1) && v->refcount == 1) ? v : NULL;
        result = op->v1->obj->calc1(op);
        val_destroy(v);
        return result;
    default: break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Code *v1 = Code(op->v1), *v;
    Obj *o2 = op->v2;
    Error *err;
    if (op->op == &o_MEMBER) {
        if (o2->obj == SYMBOL_OBJ) {
            Symbol *v2 = Symbol(o2);
            if (v2->name.len == 10 && v2->name.data[0] == '_' && v2->name.data[1] == '_') {
                static const str_t of = {(const uint8_t *)"__offset__", 10};
                str_t cf;
                str_cfcpy(&cf, &v2->name);
                if (str_cmp(&cf, &of) == 0) {
                    if (diagnostics.case_symbol && str_cmp(&v2->name, &cf) != 0) err_msg_symbol_case(&v2->name, NULL, op->epoint2);
                    return Obj(int_from_uval(code_address(v1) & all_mem2));
                }
            }
        }
        return namespace_member(op, v1->names);
    }
    if (op->op == &o_X) {
        if (o2 == Obj(none_value) || o2->obj == ERROR_OBJ) return val_reference(o2);
        return obj_oper_error(op);
    }
    if (op->op == &o_LAND || op->op == &o_LOR) {
        Obj *result = truth(Obj(v1), TRUTH_BOOL, op->epoint);
        bool i;
        if (result->obj != BOOL_OBJ) return result;
        i = (result == Obj(true_value)) != (op->op == &o_LOR);
        val_destroy(result);
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        return val_reference(i ? o2 : Obj(v1));
    }
    switch (o2->obj->type) {
    case T_CODE:
        {
            Obj *tmp1, *tmp2, *result;
            Code *v2 = Code(o2);
            err = access_check(v1, op->epoint);
            if (err != NULL) return Obj(err);
            err = access_check(v2, op->epoint2);
            if (err != NULL) return Obj(err);
            if (op->op->op == O_SUB) {
                address_t addr1 = ldigit(v1, op->epoint);
                address_t addr2 = ldigit(v2, op->epoint2);
                return Obj(int_from_ival((ival_t)addr1 - (ival_t)addr2));
            }
            tmp1 = get_code_address(v1, op->epoint);
            tmp2 = get_code_address(v2, op->epoint2);
            op->v1 = tmp1;
            op->v2 = tmp2;
            op->inplace = (op->inplace == Obj(v1) && tmp1->refcount == 1) ? tmp1 : NULL;
            result = op->v1->obj->calc2(op);
            val_destroy(tmp2);
            val_destroy(tmp1);
            return result;
        }
    case T_BOOL:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        /* fall through */
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_STR:
    case T_BYTES:
    case T_ADDRESS:
        {
            Obj *tmp, *result;
            switch (op->op->op) {
            case O_ADD:
            case O_SUB:
                {
                    bool inplace;
                    ival_t iv;
                    err = o2->obj->ival(o2, &iv, 30, op->epoint2);
                    if (err != NULL) return Obj(err);
                    if (iv == 0) return val_reference(Obj(v1));
                    inplace = (op->inplace == Obj(v1));
                    if (inplace) {
                        v = Code(val_reference(Obj(v1)));
                    } else {
                        v = new_code();
                        memcpy(((unsigned char *)v) + sizeof(Obj), ((unsigned char *)v1) + sizeof(Obj), sizeof(Code) - sizeof(Obj));
                        v->memblocks = ref_memblocks(v1->memblocks);
                        v->names = ref_namespace(v1->names);
                        v->typ = val_reference(v1->typ);
                    }
                    if (op->op->op == O_ADD) { v->offs += iv; } else { v->offs -= iv; }
                    if (v->offs >= 1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = 1073741823; }
                    if (v->offs < -1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = -1073741824; }
                    return Obj(v);
                }
            default: break;
            }
            err = access_check(v1, op->epoint);
            if (err != NULL) return Obj(err);
            tmp = get_code_address(v1, op->epoint);
            op->v1 = tmp;
            op->inplace = (op->inplace == Obj(v1) && tmp->refcount == 1) ? tmp : NULL;
            result = op->v1->obj->calc2(op);
            val_destroy(tmp);
            return result;
        }
    default:
        return o2->obj->rcalc2(op);
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    Code *v2 = Code(op->v2), *v;
    Obj *o1 = op->v1;
    Error *err;
    if (op->op == &o_IN) {
        struct oper_s oper;
        address_t ln, ln2;
        size_t i;
        ssize_t offs;
        Obj *tmp, *result;

        ln2 = (v2->dtype < 0) ? (address_t)-v2->dtype : (address_t)v2->dtype;
        if (ln2 == 0) ln2 = 1;
        ln = calc_size(v2) / ln2;

        if (ln == 0) {
            return Obj(ref_bool(false_value));
        }

        if (v2->offs >= 0) {
            offs = (ssize_t)(((uval_t)v2->offs + ln2 - 1) / ln2);
        } else {
            offs = -(ssize_t)(((uval_t)-v2->offs + ln2 - 1) / ln2);
        }

        oper.op = &o_EQ;
        oper.epoint = op->epoint;
        oper.epoint2 = op->epoint2;
        oper.epoint3 = op->epoint3;
        for (i = 0; i < ln; i++) {
            tmp = code_item(v2, (ssize_t)i + offs, ln2);
            oper.v1 = tmp;
            oper.v2 = o1;
            oper.inplace = NULL;
            result = tmp->obj->calc2(&oper);
            val_destroy(tmp);
            if (Bool(result) == true_value) return result;
            val_destroy(result);
        }
        return Obj(ref_bool(false_value));
    }
    switch (o1->obj->type) {
    case T_BOOL:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        /* fall through */
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_ADDRESS:
        {
            Obj *tmp, *result;
            if (op->op == &o_ADD) {
                ival_t iv;
                err = o1->obj->ival(o1, &iv, 30, op->epoint);
                if (err != NULL) return Obj(err);
                v = new_code();
                memcpy(((unsigned char *)v) + sizeof(Obj), ((unsigned char *)v2) + sizeof(Obj), sizeof(Code) - sizeof(Obj));
                v->memblocks = ref_memblocks(v2->memblocks);
                v->names = ref_namespace(v2->names);
                v->typ = val_reference(v2->typ);
                v->offs += iv;
                if (v->offs >= 1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = 1073741823; }
                if (v->offs < -1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = -1073741824; }
                return Obj(v);
            }
            err = access_check(v2, op->epoint2);
            if (err != NULL) return Obj(err);
            tmp = get_code_address(v2, op->epoint2);
            op->v2 = tmp;
            op->inplace = NULL;
            result = o1->obj->calc2(op);
            val_destroy(tmp);
            return result;
        }
    default: break;
    }
    return obj_oper_error(op);
}

void codeobj_init(void) {
    new_type(&obj, T_CODE, "code", sizeof(Code));
    obj.create = create;
    obj.destroy = destroy;
    obj.garbage = garbage;
    obj.same = same;
    obj.truth = truth;
    obj.repr = repr;
    obj.str = str;
    obj.ival = ival;
    obj.uval = uval;
    obj.uval2 = uval;
    obj.address = address;
    obj.iaddress = iaddress;
    obj.uaddress = uaddress;
    obj.sign = sign;
    obj.function = function;
    obj.len = len;
    obj.size = size;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
    obj.slice = slice;
}

void codeobj_names(void) {
    new_builtin("code", val_reference(Obj(CODE_OBJ)));
}
