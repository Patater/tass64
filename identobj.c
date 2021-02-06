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
#include "identobj.h"
#include <string.h>
#include "eval.h"
#include "unicode.h"
#include "error.h"
#include "file.h"
#include "values.h"

#include "typeobj.h"
#include "operobj.h"
#include "strobj.h"
#include "errorobj.h"

static Type obj;

Type *const IDENT_OBJ = &obj;

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_IDENT: return val_reference(v1);
    default: break;
    }
    return (Obj *)new_error_conv(v1, IDENT_OBJ, epoint);
}

Ident *new_ident(const str_t *name) {
    Ident *idn = (Ident *)val_alloc(IDENT_OBJ);
    if ((size_t)(name->data - current_file_list->file->data) < current_file_list->file->len) idn->name = *name;
    else str_cpy(&idn->name, name);
    idn->cfname.data = NULL;
    idn->cfname.len = 0;
    idn->hash = -1;
    idn->file_list = current_file_list;
    return idn;
}

static FAST_CALL NO_INLINE void ident_destroy(Ident *v1) {
    free((char *)v1->name.data);
}

static FAST_CALL NO_INLINE bool ident_same(const Ident *v1, const Ident *v2) {
    return memcmp(v1->name.data, v2->name.data, v2->name.len) == 0;
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Ident *v1 = (const Ident *)o1, *v2 = (const Ident *)o2;
    if (o1->obj != o2->obj || v1->name.len != v2->name.len) return false;
    switch (v1->name.len) {
    case 0: return true;
    case 1: return v1->name.data[0] == v2->name.data[0];
    default: return v1->name.data == v2->name.data || ident_same(v1, v2);
    }
}

static FAST_CALL void destroy(Obj *o1) {
    Ident *v1 = (Ident *)o1;
    const struct file_s *cfile = v1->file_list->file;
    if ((size_t)(v1->name.data - cfile->data) >= cfile->len) ident_destroy(v1);
    if (v1->cfname.data != NULL && v1->name.data != v1->cfname.data) free((uint8_t *)v1->cfname.data);
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t UNUSED(epoint), size_t maxsize) {
    Ident *v1 = (Ident *)o1;
    size_t chars;
    Str *v;
    size_t len;

    chars = calcpos(v1->name.data, v1->name.len) + 1;
    if (chars < 1 || chars > maxsize) return NULL;/* overflow */
    len = v1->name.len + 1;
    if (len < 1) return NULL;/* overflow */
    v = new_str2(len);
    if (v == NULL) return NULL;
    v->chars = chars;
    v->data[0] = '.';
    memcpy(v->data + 1, v1->name.data, len - 1);
    return &v->v;
}

static MUST_CHECK Obj *str(Obj *o1, linepos_t UNUSED(epoint), size_t maxsize) {
    Ident *v1 = (Ident *)o1;
    Str *v;
    size_t chars = calcpos(v1->name.data, v1->name.len);
    if (chars > maxsize) return NULL;
    v = new_str2(v1->name.len);
    if (v == NULL) return NULL;
    v->chars = chars;
    memcpy(v->data, v1->name.data, v1->name.len);
    return &v->v;
}

static MUST_CHECK struct Error *hash(Obj *o1, int *hs, linepos_t UNUSED(epoint)) {
    Ident *v1 = (Ident *)o1;
    str_t s = v1->cfname;
    size_t l;
    unsigned int h;
    if (s.len == 0) {
        *hs = 0;
        return NULL;
    }
    if (v1->hash >= 0) {
        *hs = v1->hash;
        return NULL;
    }
    if (s.data == NULL) {
        str_cfcpy(&s, &((Ident *)o1)->name);
        if (s.data != ((Ident *)o1)->name.data) str_cfcpy(&s, NULL);
    }
    h = (unsigned int)*s.data << 7;
    l = s.len;
    while ((l--) != 0) h = (1000003 * h) ^ *s.data++;
    h ^= s.len;
    h &= ((~0U) >> 1);
    v1->hash = h;
    *hs = h;
    return NULL;
}

static inline int icmp(oper_t op) {
    str_t *v1 = &((Ident *)op->v1)->cfname, *v2 = &((Ident *)op->v2)->cfname;
    int h;
    if (v1->data == NULL) {
        str_cfcpy(v1, &((Ident *)op->v1)->name);
        if (v1->data != ((Ident *)op->v1)->name.data) str_cfcpy(v1, NULL);
    }
    if (v2->data == NULL) {
        str_cfcpy(v2, &((Ident *)op->v2)->name);
        if (v2->data != ((Ident *)op->v2)->name.data) str_cfcpy(v2, NULL);
    }
    h = memcmp(v1->data, v2->data, (v1->len < v2->len) ? v1->len : v2->len);
    if (h != 0) return h;
    if (v1->len < v2->len) return -1;
    return (v1->len > v2->len) ? 1 : 0;
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *o2 = op->v2;
    switch (o2->obj->type) {
    case T_IDENT:
        return obj_oper_compare(op, icmp(op));
    case T_NONE:
    case T_ERROR:
        return val_reference(o2);
    default:
        if (op->op != &o_MEMBER && op->op != &o_X) {
            return o2->obj->rcalc2(op);
        }
        break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    if (op->op == &o_MEMBER) {
        return op->v1->obj->calc2(op);
    }
    return obj_oper_error(op);
}

void identobj_init(void) {
    new_type(&obj, T_IDENT, "ident", sizeof(Ident));
    obj.create = create;
    obj.destroy = destroy;
    obj.same = same;
    obj.hash = hash;
    obj.repr = repr;
    obj.str = str;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
}
