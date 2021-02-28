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
#include "functionobj.h"
#include <string.h>
#include "math.h"
#include "isnprintf.h"
#include "eval.h"
#include "variables.h"
#include "error.h"
#include "file.h"
#include "arguments.h"
#include "instruction.h"
#include "64tass.h"
#include "section.h"

#include "floatobj.h"
#include "strobj.h"
#include "listobj.h"
#include "intobj.h"
#include "boolobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"
#include "bytesobj.h"
#include "dictobj.h"
#include "addressobj.h"

static Type obj;

Type *const FUNCTION_OBJ = &obj;

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_FUNCTION: return val_reference(v1);
    default: break;
    }
    return (Obj *)new_error_conv(v1, FUNCTION_OBJ, epoint);
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Function *v1 = (const Function *)o1, *v2 = (const Function *)o2;
    return o2->obj == FUNCTION_OBJ && v1->func == v2->func;
}

static MUST_CHECK Error *hash(Obj *o1, int *hs, linepos_t UNUSED(epoint)) {
    Function *v1 = Function(o1);
    int h = v1->name_hash;
    if (h < 0) v1->name_hash = h = str_hash(&v1->name);
    *hs = h;
    return NULL;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    const Function *v1 = (const Function *)o1;
    uint8_t *s;
    size_t len;
    Str *v;
    if (epoint == NULL) return NULL;
    len = v1->name.len + 20;
    if (len < 20) return NULL; /* overflow */
    if (len > maxsize) return NULL;
    v = new_str2(len);
    if (v == NULL) return NULL;
    v->chars = len;
    s = v->data;
    memcpy(s, "<native_function '", 18);
    s += 18;
    memcpy(s, v1->name.data, v1->name.len);
    s += v1->name.len;
    *s = '\'';
    s[1] = '>';
    return &v->v;
}

static MUST_CHECK Obj *str(Obj *o1, linepos_t UNUSED(epoint), size_t maxsize) {
    const Function *v1 = (const Function *)o1;
    Str *v;
    if (v1->name.len > maxsize) return NULL;
    v = new_str2(v1->name.len);
    if (v == NULL) return NULL;
    v->chars = v1->name.len;
    memcpy(v->data, v1->name.data, v1->name.len);
    return &v->v;
}

typedef MUST_CHECK Obj *(*func_t)(oper_t op);

static MUST_CHECK Obj *gen_broadcast(oper_t op, func_t f) {
    Funcargs *vals = Funcargs(op->v2);
    struct values_s *v = vals->val;
    size_t args = vals->len;
    size_t j, k, ln = 1;
    List *vv;
    struct elements_s {
        Obj *oval;
        struct iter_s iters;
    };
    struct elements_s elements3[3], *elements = NULL;

    for (j = 0; j < args; j++) {
        const Type *objt = v[j].val->obj;
        if (objt->iterable) {
            struct iter_s *iter;
            if (elements == NULL) {
                if (args <= lenof(elements3)) {
                    elements = elements3; 
                } else {
                    if (args > SIZE_MAX / sizeof *elements) goto failed; /* overflow */
                    elements = (struct elements_s *)malloc(args * sizeof *elements);
                    if (elements == NULL) goto failed;
                }
                k = j;
            }
            iter = &elements[j].iters;
            elements[j].oval = iter->data = v[j].val; objt->getiter(iter);
            if (iter->len == 1) {
                v[j].val = iter->next(iter);
            } else if (iter->len != ln) {
                if (ln != 1) {
                    Error *err = new_error(ERROR_CANT_BROADCAS, &v[j].epoint);
                    err->u.broadcast.v1 = ln;
                    err->u.broadcast.v2 = iter->len;
                    for (; k < j + 1; k++) {
                        if (elements[k].oval == NULL) continue;
                        v[k].val = elements[k].oval;
                        iter_destroy(&elements[k].iters);
                    }
                    if (elements != elements3) free(elements);
                    return &err->v;
                }
                ln = iter->len;
            }
        } else if (elements != NULL) {
            elements[j].oval = NULL;
        }
    }
    if (elements == NULL) {
        return f(op);
    }
    if (ln != 0) {
        size_t i;
        vv = List(val_alloc(v[k].val->obj == TUPLE_OBJ ? TUPLE_OBJ : LIST_OBJ));
        Obj **vals2 = vv->data = list_create_elements(vv, ln);
        for (i = 0; i < ln; i++) {
            for (j = k; j < args; j++) {
                if (elements[j].oval == NULL) continue;
                if (elements[j].iters.len != 1) v[j].val = elements[j].iters.next(&elements[j].iters);
            }
            vals2[i] = gen_broadcast(op, f);
        }
        vv->len = i;
    } else {
        vv = List(val_reference(v[k].val->obj == TUPLE_OBJ ? &null_tuple->v : &null_list->v));
    }
    for (; k < args; k++) {
        if (elements[k].oval == NULL) continue;
        v[k].val = elements[k].oval;
        iter_destroy(&elements[k].iters);
    }
    if (elements != elements3) free(elements);
    return &vv->v;
failed:
    return (Obj *)new_error_mem(op->epoint);
}

/* range([start],end,[step]) */
static MUST_CHECK Obj *function_range(oper_t op) {
    Funcargs *vals = Funcargs(op->v2);
    struct values_s *v = vals->val;
    List *new_value;
    Error *err = NULL;
    ival_t start = 0, end, step = 1;
    size_t len2, i;
    Obj **val;

    switch (vals->len) {
    default: end = 0; break; /* impossible */
    case 1:
        err = v[0].val->obj->ival(v[0].val, &end, 8 * sizeof end, &v[0].epoint);
        break;
    case 3:
        err = v[2].val->obj->ival(v[2].val, &step, 8 * sizeof step, &v[2].epoint);
        if (err != NULL) return &err->v;
        /* fall through */
    case 2:
        err = v[0].val->obj->ival(v[0].val, &start, 8 * sizeof start, &v[0].epoint);
        if (err != NULL) return &err->v;
        err = v[1].val->obj->ival(v[1].val, &end, 8 * sizeof end, &v[1].epoint);
        break;
    }
    if (err != NULL) return &err->v;
    if (step == 0) {
        return (Obj *)new_error(ERROR_NO_ZERO_VALUE, &v[2].epoint);
    }
    if (step > 0) {
        if (end < start) end = start;
        len2 = (uval_t)(end - start + step - 1) / (uval_t)step;
    } else {
        if (end > start) end = start;
        len2 = (uval_t)(start - end - step - 1) / (uval_t)-step;
    }
    new_value = new_list();
    val = list_create_elements(new_value, len2);
    for (i = 0; i < len2; i++) {
        val[i] = (Obj *)int_from_ival(start);
        start += step;
    }
    new_value->len = len2;
    new_value->data = val;
    return &new_value->v;
}

static uint64_t state[2];

static uint64_t random64(void) {
    uint64_t a = state[0];
    const uint64_t b = state[1];
    state[0] = b;
    a ^= a << 23;
    a ^= a >> 17;
    a ^= b ^ (b >> 26);
    state[1] = a;
    return a + b;
}

void random_reseed(Obj *o1, linepos_t epoint) {
    Obj *v = INT_OBJ->create(o1, epoint);
    if (v->obj != INT_OBJ) {
        if (v == &none_value->v) err_msg_still_none(NULL, epoint);
        else if (v->obj == ERROR_OBJ) err_msg_output(Error(v));
    } else {
        Int *v1 = Int(v);
        Error *err;

        state[0] = (((uint64_t)0x5229a30f) << 32) | (uint64_t)0x996ad7eb;
        state[1] = (((uint64_t)0xc03bbc75) << 32) | (uint64_t)0x3f671f6f;

        switch (v1->len) {
        case 4: state[1] ^= ((uint64_t)v1->data[3] << (8 * sizeof *v1->data)); /* fall through */
        case 3: state[1] ^= v1->data[2]; /* fall through */
        case 2: state[0] ^= ((uint64_t)v1->data[1] << (8 * sizeof *v1->data)); /* fall through */
        case 1: state[0] ^= v1->data[0]; /* fall through */
        case 0: break;
        default:
            err = new_error(v1->len < 0 ? ERROR______NOT_UVAL : ERROR_____CANT_UVAL, epoint);
            err->u.intconv.bits = 128;
            err->u.intconv.val = val_reference(o1);
            err_msg_output_and_destroy(err);
        }
    }
    val_destroy(v);
}

/* random() */
static MUST_CHECK Obj *function_random(oper_t op) {
    Funcargs *vals = Funcargs(op->v2);
    struct values_s *v = vals->val;
    Error *err = NULL;
    ival_t start = 0, end, step = 1;
    uval_t len2;

    switch (vals->len) {
    default:
        return (Obj *)new_float((double)(random64() & (((uint64_t)1 << 53) - 1)) * ldexp(1, -53));
    case 1:
        err = v[0].val->obj->ival(v[0].val, &end, 8 * sizeof end, &v[0].epoint);
        break;
    case 3:
        err = v[2].val->obj->ival(v[2].val, &step, 8 * sizeof step, &v[2].epoint);
        if (err != NULL) return &err->v;
        /* fall through */
    case 2:
        err = v[0].val->obj->ival(v[0].val, &start, 8 * sizeof start, &v[0].epoint);
        if (err != NULL) return &err->v;
        err = v[1].val->obj->ival(v[1].val, &end, 8 * sizeof end, &v[1].epoint);
        break;
    }
    if (err != NULL) return &err->v;
    if (step == 0) {
        return (Obj *)new_error(ERROR_NO_ZERO_VALUE, &v[2].epoint);
    }
    if (step > 0) {
        if (end < start) end = start;
        len2 = (uval_t)(end - start + step - 1) / (uval_t)step;
    } else {
        if (end > start) end = start;
        len2 = (uval_t)(start - end - step - 1) / (uval_t)-step;
    }
    if (len2 != 0) {
        if (step != 1 || (len2 & (len2 - 1)) != 0) {
            uval_t a = (~(uval_t)0) / len2;
            uval_t b = a * len2;
            uval_t r;
            do {
                r = (uval_t)random64();
            } while (r >= b);
            return (Obj *)int_from_ival(start + (ival_t)(r / a) * step);
        }
        return (Obj *)int_from_ival(start + (ival_t)(random64() & (len2 - 1)));
    }
    return (Obj *)new_error(ERROR___EMPTY_RANGE, op->epoint);
}

static struct oper_s sort_tmp;
static Obj *sort_error;
static Obj *sort_val;

static int sortcomp(void) {
    int ret;
    Obj *result;
    Obj *o1 = sort_tmp.v1;
    Obj *o2 = sort_tmp.v2;
    sort_tmp.inplace = NULL;
    result = sort_tmp.v1->obj->calc2(&sort_tmp);
    if (result->obj == INT_OBJ) ret = (int)Int(result)->len;
    else {
        ret = 0;
        if (sort_error == NULL) {
            if (result->obj == ERROR_OBJ) sort_error = val_reference(result);
            else {
                if (result->obj == TUPLE_OBJ || result->obj == LIST_OBJ) {
                    List *v1 = List(result);
                    size_t i;
                    for (i = 0; i < v1->len; i++) {
                        Obj *v2 = v1->data[i];
                        if (v2->obj == INT_OBJ) {
                            ret = (int)Int(v2)->len;
                            if (ret == 0) continue;
                            val_destroy(result);
                            return ret;
                        }
                        break;
                    }
                    if (i == v1->len) {
                        val_destroy(result);
                        return 0;
                    }
                }
                sort_tmp.v1 = o1;
                sort_tmp.v2 = o2;
                sort_error = obj_oper_error(&sort_tmp);
            }
        }
    }
    val_destroy(result);
    return ret;
}

static int list_sortcomp(const void *a, const void *b) {
    int ret;
    size_t aa = *(const size_t *)a, bb = *(const size_t *)b;
    List *list = List(sort_val);
    sort_tmp.v1 = list->data[aa];
    sort_tmp.v2 = list->data[bb];
    ret = sortcomp();
    if (ret == 0) return (aa > bb) ? 1 : -1;
    return ret;
}

static int dict_sortcomp(const void *a, const void *b) {
    int ret;
    size_t aa = *(const size_t *)a, bb = *(const size_t *)b;
    Dict *dict = Dict(sort_val);
    sort_tmp.v1 = dict->data[aa].key;
    sort_tmp.v2 = dict->data[bb].key;
    ret = sortcomp();
    if (ret == 0) return (aa > bb) ? 1 : -1;
    return ret;
}

/* sort() */
static MUST_CHECK Obj *function_sort(Obj *o1, linepos_t epoint) {
    if (o1->obj == TUPLE_OBJ || o1->obj == LIST_OBJ || o1->obj == DICT_OBJ) {
        size_t ln = (o1->obj == DICT_OBJ) ? Dict(o1)->len : List(o1)->len;
        if (ln > 1) {
            size_t i;
            Obj **vals;
            size_t *sort_index;
            if (ln > SIZE_MAX / sizeof *sort_index) goto failed; /* overflow */
            sort_index = (size_t *)malloc(ln * sizeof *sort_index);
            if (sort_index == NULL) goto failed;
            for (i = 0; i < ln; i++) sort_index[i] = i;
            sort_val = o1;
            sort_error = NULL;
            sort_tmp.op = &o_CMP;
            sort_tmp.epoint = sort_tmp.epoint2 = sort_tmp.epoint3 = epoint;
            qsort(sort_index, ln, sizeof *sort_index, (o1->obj == DICT_OBJ) ? dict_sortcomp : list_sortcomp);
            if (sort_error != NULL) {
                free(sort_index);
                return sort_error;
            }
            if (o1->obj == DICT_OBJ) o1 = dict_sort(Dict(o1), sort_index);
            else {
                List *v = List(val_alloc(o1->obj));
                v->data = vals = list_create_elements(v, ln);
                v->len = ln;
                for (i = 0; i < ln; i++) vals[i] = val_reference(List(o1)->data[sort_index[i]]);
                o1 = &v->v;
            }
            free(sort_index);
            return o1;
        }
    }
    return val_reference(o1);
failed:
    return (Obj *)new_error_mem(epoint);
}

/* binary(name,[start],[length]) */
static MUST_CHECK Obj *function_binary(oper_t op) {
    Funcargs *vals = Funcargs(op->v2);
    struct values_s *v = vals->val;
    Error *err;
    ival_t offs = 0;
    uval_t length = (uval_t)-1;
    struct file_s *cfile2 = NULL;
    str_t filename;

    if (!tostr(&v[0], &filename)) {
        char *path = get_path(&filename, current_file_list->file->realname);
        cfile2 = openfile(path, current_file_list->file->realname, 1, &filename, &v[0].epoint);
        free(path);
    }

    switch (vals->len) {
    case 3:
        err = v[2].val->obj->uval(v[2].val, &length, 8 * sizeof length, &v[2].epoint);
        if (err != NULL) return &err->v;
        /* fall through */
    case 2:
        err = v[1].val->obj->ival(v[1].val, &offs, 8 * sizeof offs, &v[1].epoint);
        if (err != NULL) return &err->v;
        /* fall through */
    default:
        break;
    }
    
    if (cfile2 != NULL) {
        size_t offset, ln = cfile2->len;
        Bytes *b;
        if (offs < 0) offset = ((uval_t)-offs < ln) ? (ln - (uval_t)-offs) : 0;
        else offset = (uval_t)offs;
        if (offset < ln) ln -= offset; else ln = 0;
        if (length < ln) ln = length;
        if (ln == 0) return (Obj *)ref_bytes(null_bytes);
        if (ln > SSIZE_MAX) return (Obj *)new_error_mem(op->epoint);
        b = new_bytes(ln);
        b->len = (ssize_t)ln;
        memcpy(b->data, cfile2->data + offset, ln);
        return &b->v;
    }
    return (Obj *)ref_none();
}

static Obj *function_unsigned_bytes(oper_t op, unsigned int bits) {
    uval_t uv;
    if (touval(op->v2, &uv, bits, op->epoint2)) uv = 0;
    return (Obj *)bytes_from_uval(uv, bits >> 3);
}

static Obj *function_signed_bytes(oper_t op, unsigned int bits) {
    ival_t iv;
    if (toival(op->v2, &iv, bits, op->epoint2)) iv = 0;
    return (Obj *)bytes_from_uval((uval_t)iv, bits >> 3);
}

static Obj *function_rta_addr(oper_t op, bool rta) {
    uval_t uv;
    Obj *val = op->v2;
    atype_t am = val->obj->address(val);
    if (touaddress(val, &uv, (am == A_KR) ? 16 : all_mem_bits, op->epoint2)) {
        uv = 0;
    } else {
        uv &= all_mem;
        switch (am) {
        case A_NONE:
            if ((current_address->l_address ^ uv) > 0xffff) err_msg2(ERROR_CANT_CROSS_BA, val, op->epoint2);
            break;
        case A_KR:
            break;
        default:
            err_msg_output_and_destroy(err_addressing(am, op->epoint2, -1));
        }
        if (rta) uv--;
    }
    return (Obj *)bytes_from_uval(uv, 2);
}

static MUST_CHECK Obj *apply_func(oper_t op) {
    Obj *o2 = op->v2;
    const Type *typ = o2->obj;
    bool inplace = op->inplace == o2;
    double real;
    Error_types err;

    if (typ->iterable) {
        List *v;
        size_t i, len;
        Obj **vals;

        if (!inplace || (typ != TUPLE_OBJ && typ != LIST_OBJ)) {
            struct iter_s iter;
            iter.data = o2; typ->getiter(&iter);
            if (iter.len == 0) {
                iter_destroy(&iter);
                return val_reference(typ == TUPLE_OBJ ? &null_tuple->v : &null_list->v);
            }
            v = List(val_alloc(typ == TUPLE_OBJ ? TUPLE_OBJ : LIST_OBJ));
            v->data = vals = list_create_elements(v, iter.len);
            for (i = 0; i < iter.len && (o2 = iter.next(&iter)) != NULL; i++) {
                op->v2 = o2;
                op->inplace = inplace && o2->refcount == 1 ? o2 : NULL;
                vals[i] = apply_func(op);
            }
            iter_destroy(&iter);
            v->len = i;
            return &v->v;
        } 
        v = ref_list(List(o2));
        len = v->len;
        vals = v->data;
        for (i = 0; i < len; i++) {
            op->v2 = o2 = vals[i];
            op->inplace = o2->refcount == 1 ? o2 : NULL;
            vals[i] = apply_func(op);
            val_destroy(o2);
        }
        return &v->v;
    }
    if (op->v1->obj != FUNCTION_OBJ) return Type(op->v1)->create(op->v2, op->epoint2);
    switch (Function(op->v1)->func) {
    case F_SIZE: return typ->size(op);
    case F_SIGN: return typ->sign(o2, op->epoint2);
    case F_BYTE: return function_unsigned_bytes(op, 8);
    case F_WORD: return function_unsigned_bytes(op, 16);
    case F_LONG: return function_unsigned_bytes(op, 24);
    case F_DWORD: return function_unsigned_bytes(op, 32);
    case F_CHAR: return function_signed_bytes(op, 8);
    case F_SINT: return function_signed_bytes(op, 16);
    case F_LINT: return function_signed_bytes(op, 24);
    case F_DINT: return function_signed_bytes(op, 32);
    case F_ADDR: return function_rta_addr(op, false);
    case F_RTA: return function_rta_addr(op, true);
    case F_CEIL:
    case F_FLOOR:
    case F_ROUND:
    case F_TRUNC:
    case F_ABS: return typ->function(op);
    case F_REPR:
        {
            Obj *v = typ->repr(o2, op->epoint2, SIZE_MAX);
            return v != NULL ? v : (Obj *)new_error_mem(op->epoint2);
        }
    default: break;
    }
    if (typ != FLOAT_OBJ) {
        o2 = FLOAT_OBJ->create(o2, op->epoint2);
        if (o2->obj != FLOAT_OBJ) return o2;
        inplace = o2->refcount == 1;
    }
    real = Float(o2)->real;
    switch (Function(op->v1)->func) {
    case F_SQRT:
        if (real < 0.0) {
            err = ERROR_SQUARE_ROOT_N; goto failed;
        }
        real = sqrt(real);
        break;
    case F_LOG10:
        if (real <= 0.0) {
            err = ERROR_LOG_NON_POSIT; goto failed;
        }
        real = log10(real);
        break;
    case F_LOG:
        if (real <= 0.0) {
            err = ERROR_LOG_NON_POSIT; goto failed;
        }
        real = log(real);
        break;
    case F_EXP: real = exp(real);break;
    case F_SIN: real = sin(real);break;
    case F_COS: real = cos(real);break;
    case F_TAN: real = tan(real);break;
    case F_ACOS:
        if (real < -1.0 || real > 1.0) {
            err = ERROR___MATH_DOMAIN; goto failed;
        }
        real = acos(real);
        break;
    case F_ASIN:
        if (real < -1.0 || real > 1.0) {
            err = ERROR___MATH_DOMAIN; goto failed;
        }
        real = asin(real);
        break;
    case F_ATAN: real = atan(real);break;
    case F_CBRT: real = cbrt(real);break;
    case F_FRAC: real -= trunc(real);break;
    case F_RAD: real = real * M_PI / 180.0;break;
    case F_DEG: real = real * 180.0 / M_PI;break;
    case F_COSH: real = cosh(real);break;
    case F_SINH: real = sinh(real);break;
    case F_TANH: real = tanh(real);break;
    default: real = HUGE_VAL; break; /* can't happen */
    }
    if (!inplace || real == HUGE_VAL || real == -HUGE_VAL || real != real) {
        if (typ != FLOAT_OBJ) val_destroy(o2);
        return float_from_double(real, op->epoint2);
    }
    Float(o2)->real = real;
    return typ != FLOAT_OBJ ? o2 : val_reference(o2);
failed:
    if (typ != FLOAT_OBJ) val_destroy(o2);
    return (Obj *)new_error_obj(err, op->v2, op->epoint2);
}

static MUST_CHECK Obj *to_real(struct values_s *v, double *r) {
    if (v->val->obj == FLOAT_OBJ) {
        *r = Float(v->val)->real;
    } else {
        Obj *val = FLOAT_OBJ->create(v->val, &v->epoint);
        if (val->obj != FLOAT_OBJ) return val;
        *r = Float(val)->real;
        val_destroy(val);
    }
    return NULL;
}

static MUST_CHECK Obj *function_hypot(oper_t op) {
    struct values_s *v = Funcargs(op->v2)->val;
    Obj *val;
    double real, real2;

    val = to_real(&v[0], &real);
    if (val != NULL) return val;
    val = to_real(&v[1], &real2);
    if (val != NULL) return val;
    return float_from_double(hypot(real, real2), op->epoint);
}

static MUST_CHECK Obj *function_atan2(oper_t op) {
    struct values_s *v = Funcargs(op->v2)->val;
    Obj *val;
    double real, real2;

    val = to_real(&v[0], &real);
    if (val != NULL) return val;
    val = to_real(&v[1], &real2);
    if (val != NULL) return val;
    return float_from_double(atan2(real, real2), op->epoint);
}

static MUST_CHECK Obj *function_pow(oper_t op) {
    struct values_s *v = Funcargs(op->v2)->val;
    Obj *val;
    double real, real2;

    val = to_real(&v[0], &real);
    if (val != NULL) return val;
    val = to_real(&v[1], &real2);
    if (val != NULL) return val;
    if (real2 < 0.0 && real == 0.0) {
        return (Obj *)new_error(ERROR_DIVISION_BY_Z, op->epoint);
    }
    if (real < 0.0 && floor(real2) != real2) {
        return (Obj *)new_error(ERROR_NEGFRAC_POWER, op->epoint);
    }
    return float_from_double(pow(real, real2), op->epoint);
}

static inline int icmp(oper_t op) {
    Function_types v1 = Function(op->v1)->func;
    Function_types v2 = Function(op->v2)->func;
    if (v1 < v2) return -1;
    return (v1 > v2) ? 1 : 0;
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Function *v1 = Function(op->v1);
    Obj *o2 = op->v2;
    Function_types func;
    struct values_s *v;
    size_t args;
    switch (o2->obj->type) {
    case T_FUNCTION:
        return obj_oper_compare(op, icmp(op));
    case T_FUNCARGS:
        {
            Funcargs *v2 = Funcargs(o2);
            v = v2->val;
            args = v2->len;
            switch (op->op->op) {
            case O_FUNC:
                func = v1->func;
                switch (func) {
                case F_HYPOT:
                    if (args != 2) {
                        return (Obj *)new_error_argnum(args, 2, 2, op->epoint2);
                    }
                    return gen_broadcast(op, function_hypot);
                case F_ATAN2:
                    if (args != 2) {
                        return (Obj *)new_error_argnum(args, 2, 2, op->epoint2);
                    }
                    return gen_broadcast(op, function_atan2);
                case F_POW:
                    if (args != 2) {
                        return (Obj *)new_error_argnum(args, 2, 2, op->epoint2);
                    }
                    return gen_broadcast(op, function_pow);
                case F_RANGE:
                    if (args < 1 || args > 3) {
                        return (Obj *)new_error_argnum(args, 1, 3, op->epoint2);
                    }
                    return gen_broadcast(op, function_range);
                case F_BINARY:
                    if (args < 1 || args > 3) {
                        return (Obj *)new_error_argnum(args, 1, 3, op->epoint2);
                    }
                    return gen_broadcast(op, function_binary);
                case F_FORMAT:
                    if (args < 1) {
                        return (Obj *)new_error_argnum(args, 1, 0, op->epoint2);
                    }
                    return gen_broadcast(op, isnprintf);
                case F_RANDOM:
                    if (args > 3) {
                        return (Obj *)new_error_argnum(args, 0, 3, op->epoint2);
                    }
                    return gen_broadcast(op, function_random);
                default:
                    if (args != 1) {
                        return (Obj *)new_error_argnum(args, 1, 1, op->epoint2);
                    }
                    switch (func) {
                    case F_ANY: return v[0].val->obj->truth(v[0].val, TRUTH_ANY, &v[0].epoint);
                    case F_ALL: return v[0].val->obj->truth(v[0].val, TRUTH_ALL, &v[0].epoint);
                    case F_LEN: 
                        op->v2 = v[0].val;
                        return v[0].val->obj->len(op);
                    case F_SORT: return function_sort(v[0].val, &v[0].epoint);
                    default: 
                        op->v2 = v[0].val;
                        op->inplace = v[0].val->refcount == 1 ? v[0].val : NULL;
                        return apply_func(op);
                    }
                }
            default: break;
            }
            break;
        }
    case T_NONE:
    case T_ERROR:
        return val_reference(o2);
    default:
        if (o2->obj->iterable && op->op != &o_MEMBER && op->op != &o_X) {
            return o2->obj->rcalc2(op);
        }
        break;
    }
    return obj_oper_error(op);
}

MUST_CHECK Obj *apply_convert(oper_t op) {
    struct values_s *v = Funcargs(op->v2)->val;
    op->v2 = v[0].val;
    op->inplace = v[0].val->refcount == 1 ? v[0].val : NULL;
    return apply_func(op);
}

void functionobj_init(void) {
    new_type(&obj, T_FUNCTION, "function", sizeof(Function));
    obj.create = create;
    obj.hash = hash;
    obj.same = same;
    obj.repr = repr;
    obj.str = str;
    obj.calc2 = calc2;
}

struct builtin_functions_s {
    const char *name;
    Function_types func;
};

static struct builtin_functions_s builtin_functions[] = {
    {"abs", F_ABS},
    {"acos", F_ACOS},
    {"addr", F_ADDR},
    {"all", F_ALL},
    {"any", F_ANY},
    {"asin", F_ASIN},
    {"atan", F_ATAN},
    {"atan2", F_ATAN2},
    {"binary", F_BINARY},
    {"byte", F_BYTE},
    {"cbrt", F_CBRT},
    {"ceil", F_CEIL},
    {"char", F_CHAR},
    {"cos", F_COS},
    {"cosh", F_COSH},
    {"deg", F_DEG},
    {"dint", F_DINT},
    {"dword", F_DWORD},
    {"exp", F_EXP},
    {"floor", F_FLOOR},
    {"format", F_FORMAT},
    {"frac", F_FRAC},
    {"hypot", F_HYPOT},
    {"len", F_LEN},
    {"lint", F_LINT},
    {"log", F_LOG},
    {"log10", F_LOG10},
    {"long", F_LONG},
    {"pow", F_POW},
    {"rad", F_RAD},
    {"random", F_RANDOM},
    {"range", F_RANGE},
    {"repr", F_REPR},
    {"round", F_ROUND},
    {"rta", F_RTA},
    {"sign", F_SIGN},
    {"sin", F_SIN},
    {"sinh", F_SINH},
    {"sint", F_SINT},
    {"size", F_SIZE},
    {"sort", F_SORT},
    {"sqrt", F_SQRT},
    {"tan", F_TAN},
    {"tanh", F_TANH},
    {"trunc", F_TRUNC},
    {"word", F_WORD},
    {NULL, F_NONE}
};

void functionobj_names(void) {
    int i;

    for (i = 0; builtin_functions[i].name != NULL; i++) {
        Function *func = Function(val_alloc(FUNCTION_OBJ));
        func->name.data = (const uint8_t *)builtin_functions[i].name;
        func->name.len = strlen(builtin_functions[i].name);
        func->func = builtin_functions[i].func;
        func->name_hash = -1;
        new_builtin(builtin_functions[i].name, &func->v);
    }
}
