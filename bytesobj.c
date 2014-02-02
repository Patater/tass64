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
#include "bytesobj.h"
#include "eval.h"
#include "isnprintf.h"
#include "misc.h"
#include "encoding.h"

#include "boolobj.h"

static struct obj_s obj;

obj_t BYTES_OBJ = &obj;

static void destroy(struct value_s *v1) {
    if (v1->u.bytes.val != v1->u.bytes.data) free(v1->u.bytes.data);
}

static uint8_t *bnew(struct value_s *v, size_t len) {
    if (len > sizeof(v->u.bytes.val)) {
        uint8_t *s = (uint8_t *)malloc(len);
        if (!s) err_msg_out_of_memory();
        return s;
    }
    return v->u.bytes.val;
}

static void copy(const struct value_s *v1, struct value_s *v) {
    uint8_t *s;
    v->obj = BYTES_OBJ;
    v->refcount = 1;
    v->u.bytes.len = v1->u.bytes.len;
    if (v1->u.bytes.len) {
        s = bnew(v, v->u.bytes.len);
        memcpy(s, v1->u.bytes.data, v->u.bytes.len);
    } else s = NULL;
    v->u.bytes.data = s;
}

static void copy_temp(const struct value_s *v1, struct value_s *v) {
    v->obj = BYTES_OBJ;
    v->refcount = 1;
    v->u.bytes.len = v1->u.bytes.len;
    if (v1->u.bytes.data == v1->u.bytes.val) {
        v->u.bytes.data = v->u.bytes.val;
        if (v->u.bytes.len) memcpy(v->u.bytes.data, v1->u.bytes.data, v->u.bytes.len);
    } else v->u.bytes.data = v1->u.bytes.data;
}

static int same(const struct value_s *v1, const struct value_s *v2) {
    return v2->obj == BYTES_OBJ && v1->u.bytes.len == v2->u.bytes.len && (
            v1->u.bytes.data == v2->u.bytes.data ||
            !memcmp(v1->u.bytes.data, v2->u.bytes.data, v2->u.bytes.len));
}

static int truth(const struct value_s *v1, struct value_s *v, enum truth_e type, linepos_t epoint) {
    size_t i;
    int result;
    switch (type) {
    case TRUTH_ALL:
        result = 1;
        for (i = 0; i < v1->u.bytes.len; i++) {
            if (!v1->u.bytes.data[i]) {result = 0; break;}
        }
        break;
    case TRUTH_ANY:
    case TRUTH_BOOL:
        result = 0;
        for (i = 0; i < v1->u.bytes.len; i++) {
            if (v1->u.bytes.data[i]) {result = 1; break;}
        }
        break;
    default: 
        if (v1 == v) destroy(v);
        v->obj = ERROR_OBJ;
        v->u.error.num = ERROR_____CANT_BOOL;
        v->u.error.epoint = *epoint;
        v->u.error.u.objname = v1->obj->name;
        return 1;
    }
    if (v1 == v) destroy(v);
    bool_from_int(v, result);
    return 0;
}

static void repr(const struct value_s *v1, struct value_s *v, linepos_t UNUSED(epoint)) {
    size_t i, len, len2;
    uint8_t *s;
    struct value_s tmp;
    len2 = v1->u.bytes.len * 4;
    len = 9 - (v1->u.bytes.len > 0) + len2;
    if (len < len2 || v1->u.bytes.len > SIZE_MAX / 4) err_msg_out_of_memory(); /* overflow */
    s = str_create_elements(&tmp, len);

    memcpy(s, "bytes([", 7);
    len = 7;
    for (i = 0;i < v1->u.bytes.len; i++) {
        len += sprintf((char *)s + len, i ? ",$%02x" : "$%02x", v1->u.bytes.data[i]);
    }
    s[len++] = ']';
    s[len++] = ')';
    if (v == v1) destroy(v);
    v->obj = STR_OBJ;
    if (s == tmp.u.str.val) {
        memcpy(v->u.str.val, s, len);
        s = v->u.str.val;
    }
    v->u.str.len = len;
    v->u.str.chars = len;
    v->u.str.data = s;
}

static int hash(const struct value_s *v1, struct value_s *UNUSED(v), linepos_t UNUSED(epoint)) {
    size_t l = v1->u.bytes.len;
    const uint8_t *s2 = v1->u.bytes.data;
    unsigned int h;
    if (!l) return 0;
    h = *s2 << 7;
    while (l--) h = (1000003 * h) ^ *s2++;
    h ^= v1->u.bytes.len;
    return h & ((~(unsigned int)0) >> 1);
}

int bytes_from_str(struct value_s *v, const struct value_s *v1) {
    size_t len = v1->u.str.len, len2 = 0;
    uint8_t *s;
    struct value_s tmp;
    if (len) {
        if (actual_encoding) {
            int ch;
            if (len < sizeof(v->u.bytes.val)) len = sizeof(v->u.bytes.val);
            s = bnew(&tmp, len);
            encode_string(v1);
            while ((ch = encode_string(NULL)) != EOF) {
                if (len2 >= len) {
                    if (tmp.u.bytes.val == s) {
                        len = 16;
                        s = (uint8_t *)malloc(len);
                        memcpy(s, tmp.u.bytes.val, len2);
                    } else {
                        len += 1024;
                        if (len < 1024) err_msg_out_of_memory(); /* overflow */
                        s = (uint8_t *)realloc(s, len);
                    }
                    if (!s) err_msg_out_of_memory();
                }
                s[len2++] = ch;
            }
        } else if (v1->u.str.chars == 1) {
            uint32_t ch2 = v1->u.str.data[0];
            if (ch2 & 0x80) utf8in(v1->u.str.data, &ch2);
            s = bnew(&tmp, 3);
            s[0] = ch2;
            s[1] = ch2 >> 8;
            s[2] = ch2 >> 16;
            len2 = 3;
        } else {
            return 1;
        }
    } else s = NULL;
    if (v == v1) STR_OBJ->destroy(v);
    if (len2 <= sizeof(v->u.bytes.val)) {
        if (len2) memcpy(v->u.bytes.val, s, len2);
        if (tmp.u.bytes.val != s) free(s);
        s = v->u.bytes.val;
    } else if (len2 < len) {
        s = (uint8_t *)realloc(s, len2);
        if (!s) err_msg_out_of_memory();
    }
    v->obj = BYTES_OBJ;
    v->u.bytes.len = len2;
    v->u.bytes.data = s;
    return 0;
}

static int MUST_CHECK ival(const struct value_s *v1, struct value_s *v, ival_t *iv, int bits, linepos_t epoint) {
    struct value_s tmp;
    int ret;
    int_from_bytes(&tmp, v1);
    ret = tmp.obj->ival(&tmp, v, iv, bits, epoint);
    tmp.obj->destroy(&tmp);
    return ret;
}

static int MUST_CHECK uval(const struct value_s *v1, struct value_s *v, uval_t *uv, int bits, linepos_t epoint) {
    struct value_s tmp;
    int ret;
    int_from_bytes(&tmp, v1);
    ret = tmp.obj->uval(&tmp, v, uv, bits, epoint);
    tmp.obj->destroy(&tmp);
    return ret;
}

static int MUST_CHECK real(const struct value_s *v1, struct value_s *v, double *r, linepos_t epoint) {
    struct value_s tmp;
    int ret;
    int_from_bytes(&tmp, v1);
    ret = tmp.obj->real(&tmp, v, r, epoint);
    tmp.obj->destroy(&tmp);
    return ret;
}

static void sign(const struct value_s *v1, struct value_s *v, linepos_t UNUSED(epoint)) {
    size_t i;
    int s = 0;
    for (i = 0; i < v1->u.bytes.len; i++) {
        if (v1->u.bytes.data[i]) {s = 1; break;}
    }
    if (v1 == v) destroy(v);
    int_from_int(v, s);
}

static void absolute(const struct value_s *v1, struct value_s *v, linepos_t UNUSED(epoint)) {
    struct value_s tmp;
    int_from_bytes(&tmp, v1);
    if (v == v1) destroy(v);
    tmp.obj->copy_temp(&tmp, v);
}

static void integer(const struct value_s *v1, struct value_s *v, linepos_t UNUSED(epoint)) {
    struct value_s tmp;
    int_from_bytes(&tmp, v1);
    if (v == v1) destroy(v);
    tmp.obj->copy_temp(&tmp, v);
}

static void len(const struct value_s *v1, struct value_s *v, linepos_t UNUSED(epoint)) {
    size_t uv = v1->u.bytes.len;
    if (v1 == v) destroy(v);
    int_from_uval(v, uv);
}

static void getiter(struct value_s *v1, struct value_s *v) {
    v->obj = ITER_OBJ;
    v->u.iter.val = 0;
    v->u.iter.iter = &v->u.iter.val;
    v->u.iter.data = val_reference(v1);
}

static struct value_s *MUST_CHECK next(struct value_s *v1, struct value_s *v) {
    const struct value_s *vv1 = v1->u.iter.data;
    if (v1->u.iter.val >= vv1->u.bytes.len) return NULL;

    v->u.bytes.val[0] = vv1->u.bytes.data[v1->u.iter.val++];
    v->obj = BYTES_OBJ;
    v->u.bytes.len = 1;
    v->u.bytes.data = v->u.bytes.val;
    return v;
}

static void calc1(oper_t op) {
    struct value_s *v1 = op->v1, *v = op->v;
    struct value_s tmp;
    switch (op->op->u.oper.op) {
    case O_NEG:
    case O_POS:
    case O_STRING: int_from_bytes(&tmp, v1);
        break;
    default: bits_from_bytes(&tmp, v1);
        break;
    }
    if (v == v1) destroy(v);
    op->v1 = &tmp;
    tmp.refcount = 0;
    tmp.obj->calc1(op);
    op->v1 = v1;
    tmp.obj->destroy(&tmp);
}

static int calc2_bytes(oper_t op) {
    struct value_s *v1 = op->v1, *v2 = op->v2, *v = op->v;
    int val;
    switch (op->op->u.oper.op) {
    case O_CMP:
        {
            int h = memcmp(v1->u.bytes.data, v2->u.bytes.data, (v1->u.bytes.len < v2->u.bytes.len) ? v1->u.bytes.len : v2->u.bytes.len);
            if (h) h = (h > 0) - (h < 0);
            else h = (v1->u.bytes.len > v2->u.bytes.len) - (v1->u.bytes.len < v2->u.bytes.len);
            if (v == v1 || v == v2) destroy(v);
            int_from_int(v, h);
            return 0;
        }
    case O_EQ:
        val = (v1->u.bytes.len == v2->u.bytes.len) && (v1->u.bytes.data == v2->u.bytes.data || !memcmp(v1->u.bytes.data, v2->u.bytes.data, v1->u.bytes.len));
        break;
    case O_NE:
        val = (v1->u.bytes.len != v2->u.bytes.len) || (v1->u.bytes.data != v2->u.bytes.data && memcmp(v1->u.bytes.data, v2->u.bytes.data, v1->u.bytes.len));
        break;
    case O_LT:
        val = memcmp(v1->u.bytes.data, v2->u.bytes.data, (v1->u.bytes.len < v2->u.bytes.len) ? v1->u.bytes.len:v2->u.bytes.len);
        if (!val) val = v1->u.bytes.len < v2->u.bytes.len; else val = val < 0;
        break;
    case O_GT:
        val = memcmp(v1->u.bytes.data, v2->u.bytes.data, (v1->u.bytes.len < v2->u.bytes.len) ? v1->u.bytes.len:v2->u.bytes.len);
        if (!val) val = v1->u.bytes.len > v2->u.bytes.len; else val = val > 0;
        break;
    case O_LE:
        val = memcmp(v1->u.bytes.data, v2->u.bytes.data, (v1->u.bytes.len < v2->u.bytes.len) ? v1->u.bytes.len:v2->u.bytes.len);
        if (!val) val = v1->u.bytes.len <= v2->u.bytes.len; else val = val <= 0;
        break;
    case O_GE:
        val = memcmp(v1->u.bytes.data, v2->u.bytes.data, (v1->u.bytes.len < v2->u.bytes.len) ? v1->u.bytes.len:v2->u.bytes.len);
        if (!val) val = v1->u.bytes.len >= v2->u.bytes.len; else val = val >= 0;
        break;
    case O_CONCAT:
        if (v == v1) {
            uint8_t *s;
            size_t ln;
            if (!v2->u.bytes.len) return 0;
            ln = v1->u.bytes.len;
            v->u.bytes.len += v2->u.bytes.len;
            if (v->u.bytes.len < v2->u.bytes.len) err_msg_out_of_memory(); /* overflow */
            s = (uint8_t *)v1->u.bytes.data;
            if (s != v1->u.bytes.val) {
                s = (uint8_t *)realloc(s, v->u.bytes.len);
                if (!s) err_msg_out_of_memory();
            } else {
                s = bnew(v, v->u.bytes.len);
                if (s != v1->u.bytes.val) memcpy(s, v1->u.bytes.val, v1->u.bytes.len);
            }
            memcpy(s + ln, v2->u.bytes.data, v2->u.bytes.len);
            v->u.bytes.data = s;
            return 0;
        }
        v->obj = BYTES_OBJ;
        v->u.bytes.len = v1->u.bytes.len + v2->u.bytes.len;
        if (v->u.bytes.len < v2->u.bytes.len) err_msg_out_of_memory(); /* overflow */
        if (v->u.bytes.len) {
            uint8_t *s = bnew(v, v->u.bytes.len);
            memcpy(s, v1->u.bytes.data, v1->u.bytes.len);
            memcpy(s + v1->u.bytes.len, v2->u.bytes.data, v2->u.bytes.len);
            v->u.bytes.data = s;
        } else v->u.bytes.data = NULL;
        return 0;
    case O_IN:
        {
            const uint8_t *c, *c2, *e;
            if (!v1->u.bytes.len) { val = 1; break; }
            if (v1->u.bytes.len > v2->u.bytes.len) { val = 0; break; }
            c2 = v2->u.bytes.data;
            e = c2 + v2->u.bytes.len - v1->u.bytes.len;
            for (;;) {
                c = (uint8_t *)memchr(c2, v1->u.bytes.data[0], e - c2 + 1);
                if (!c) { val = 0; break; }
                if (!memcmp(c, v1->u.bytes.data, v1->u.bytes.len)) { val = 1; break; }
                c2 = c + 1;
            }
            break;
        }
    default: return 1;
    }
    if (v == v1 || v == v2) destroy(v);
    bool_from_int(v, val);
    return 0;
}

static void repeat(oper_t op, uval_t rep) {
    struct value_s *v1 = op->v1, *v = op->v, tmp;
    v->obj = BYTES_OBJ;
    if (v1->u.bytes.len && rep) {
        uint8_t *s, *s2;
        size_t ln = v1->u.bytes.len;
        if (ln > SIZE_MAX / rep) err_msg_out_of_memory(); /* overflow */
        s2 = s = bnew(&tmp, ln * rep);
        v->u.bytes.len = 0;
        while (rep--) {
            memcpy(s + v->u.bytes.len, v1->u.bytes.data, ln);
            v->u.bytes.len += ln;
        }
        if (v->u.bytes.len <= sizeof(v->u.bytes.val)) {
            memcpy(v->u.bytes.val, s2, v->u.bytes.len);
            if (tmp.u.bytes.val != s2) free(s2);
            s2 = v->u.bytes.val;
        }
        v->u.bytes.data = s2;
        return;
    }
    v->u.bytes.data = v->u.bytes.val;
    v->u.bytes.len = 0;
}

static void calc2(oper_t op) {
    struct value_s *v1 = op->v1, *v2 = op->v2, *v = op->v;
    struct value_s tmp;
    switch (v2->obj->type) {
    case T_BYTES: if (calc2_bytes(op)) break; return;
    case T_BOOL:
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_CODE:
    case T_ADDRESS:
        {
            switch (op->op->u.oper.op) {
            case O_CONCAT:
            case O_AND:
            case O_OR:
            case O_XOR:
            case O_LSHIFT:
            case O_RSHIFT: bits_from_bytes(&tmp, v1); break;
            default: int_from_bytes(&tmp, v1);
            }
            if (v1 == v) v->obj->destroy(v);
            op->v1 = &tmp;
            tmp.refcount = 0;
            tmp.obj->calc2(op);
            op->v1 = v1;
            tmp.obj->destroy(&tmp);
        }
        return;
    case T_TUPLE:
    case T_LIST:
    case T_STR:
    case T_IDENT:
    case T_ANONIDENT:
    case T_GAP:
    case T_REGISTER:
        if (op->op != &o_MEMBER) {
            v2->obj->rcalc2(op); return;
        }
    default: break;
    }
    obj_oper_error(op);
}

static void rcalc2(oper_t op) {
    struct value_s *v1 = op->v1, *v2 = op->v2, *v = op->v;
    struct value_s tmp;
    switch (v1->obj->type) {
    case T_BYTES: if (calc2_bytes(op)) break; return;
    case T_BOOL:
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_CODE:
    case T_ADDRESS:
        {
            switch (op->op->u.oper.op) {
            case O_CONCAT:
            case O_AND:
            case O_OR:
            case O_XOR: bits_from_bytes(&tmp, v2); break;
            default: int_from_bytes(&tmp, v2);
            }
            if (v2 == v) v->obj->destroy(v);
            op->v2 = &tmp;
            tmp.refcount = 0;
            tmp.obj->rcalc2(op);
            op->v2 = v2;
            tmp.obj->destroy(&tmp);
        }
        return;
    case T_TUPLE:
    case T_LIST:
    case T_STR:
        if (op->op != &o_IN) {
            v1->obj->calc2(op); return;
        }
    default: break;
    }
    obj_oper_error(op); return;
}

static inline void slice(struct value_s *v1, uval_t len1, ival_t offs, ival_t end, ival_t step, struct value_s *v) {
    uint8_t *p;
    uint8_t *p2;
    struct value_s tmp;

    if (!len1) {
        if (v1 == v) destroy(v);
        copy(&null_bytes, v);return;
    }
    if (step == 1) {
        if (len1 == v1->u.bytes.len) {
            if (v1 != v) copy(v1, v);
            return; /* original bytes */
        }
        p = p2 = bnew(&tmp, len1);
        memcpy(p2, v1->u.bytes.data + offs, len1);
    } else {
        p = p2 = bnew(&tmp, len1);
        while ((end > offs && step > 0) || (end < offs && step < 0)) {
            *p2++ = v1->u.bytes.data[offs];
            offs += step;
        }
    }
    if (v == v1) destroy(v);
    if (len1 <= sizeof(v->u.bytes.val)) {
        memcpy(v->u.bytes.val, p, len1);
        p = v->u.bytes.val;
    }
    v->obj = BYTES_OBJ;
    v->u.bytes.len = len1;
    v->u.bytes.data = p;
}

static void iindex(oper_t op) {
    uint8_t *p, b;
    uint8_t *p2;
    size_t len1, len2;
    ival_t offs;
    size_t i;
    struct value_s *v1 = op->v1, *v2 = op->v2, *v = op->v, tmp, err;

    len1 = v1->u.bytes.len;

    if (v2->obj == TUPLE_OBJ || v2->obj == LIST_OBJ) {
        if (!v2->u.list.len) {
            if (v1 == v) destroy(v);
            copy(&null_bytes, v);return;
        }
        len2 = v2->u.list.len;
        p = p2 = bnew(&tmp, len2);
        for (i = 0; i < len2; i++) {
            offs = indexoffs(v2->u.list.data[i], &err, len1, &op->epoint2);
            if (offs < 0) {
                if (p != tmp.u.bytes.val) free(p);
                if (v1 == v) destroy(v);
                err.obj->copy_temp(&err, v);
                return;
            }
            *p2++ = v1->u.bytes.data[offs];
        }
        if (v == v1) destroy(v);
        if (len2 <= sizeof(v->u.bytes.val)) {
            memcpy(v->u.bytes.val, p, len2);
            p = v->u.bytes.val;
        }
        v->obj = BYTES_OBJ;
        v->u.bytes.len = len2;
        v->u.bytes.data = p;
        return;
    }
    if (v2->obj == COLONLIST_OBJ) {
        ival_t len, end, step;
        len = sliceparams(op, len1, &offs, &end, &step);
        if (len < 0) return;
        return slice(v1, len, offs, end, step, v);
    }
    offs = indexoffs(v2, &err, len1, &op->epoint2);
    if (offs < 0) {
        if (v1 == v) destroy(v);
        err.obj->copy_temp(&err, v);
        return;
    }
    b = v1->u.bytes.data[offs];
    if (v1 == v) destroy(v);
    v->obj = BYTES_OBJ;
    v->u.bytes.len = 1;
    v->u.bytes.val[0] = b;
    v->u.bytes.data = v->u.bytes.val;
}

void bytesobj_init(void) {
    obj_init(&obj, T_BYTES, "<bytes>");
    obj.destroy = destroy;
    obj.copy = copy;
    obj.copy_temp = copy_temp;
    obj.same = same;
    obj.truth = truth;
    obj.hash = hash;
    obj.repr = repr;
    obj.ival = ival;
    obj.uval = uval;
    obj.real = real;
    obj.sign = sign;
    obj.abs = absolute;
    obj.integer = integer;
    obj.len = len;
    obj.getiter = getiter;
    obj.next = next;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
    obj.repeat = repeat;
    obj.iindex = iindex;
}
