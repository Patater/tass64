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

#include "typeobj.h"
#include "operobj.h"
#include "strobj.h"

static Type ident_obj;
static Type anonident_obj;

Type *const IDENT_OBJ = &ident_obj;
Type *const ANONIDENT_OBJ = &anonident_obj;

static MUST_CHECK Obj *repr(Obj *o1, linepos_t UNUSED(epoint), size_t maxsize) {
    Ident *v1 = (Ident *)o1;
    Str *v;
    size_t len = v1->name.len;
    if (len > maxsize) return NULL;
    v = new_str(len);
    v->chars = calcpos(v1->name.data, len);
    memcpy(v->data, v1->name.data, len);
    return &v->v;
}

static MUST_CHECK Obj *anon_repr(Obj *o1, linepos_t UNUSED(epoint), size_t maxsize) {
    Anonident *v1 = (Anonident *)o1;
    Str *v;
    size_t len = v1->count < 0 ? -v1->count : (v1->count + 1);
    if (len > maxsize) return NULL;
    v = new_str(len);
    v->chars = len;
    memset(v->data, v1->count >= 0 ? '+' : '-', len);
    return &v->v;
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *o2 = op->v2;
    switch (o2->obj->type) {
    case T_TUPLE:
    case T_LIST:
        if (op->op != &o_MEMBER && op->op != &o_X) {
            return o2->obj->rcalc2(op);
        }
        break;
    case T_NONE:
    case T_ERROR:
        return val_reference(o2);
    default: break;
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
    new_type(&ident_obj, T_IDENT, "ident", sizeof(Ident));
    ident_obj.repr = repr;
    ident_obj.calc2 = calc2;
    ident_obj.rcalc2 = rcalc2;
    new_type(&anonident_obj, T_ANONIDENT, "anonident", sizeof(Anonident));
    anonident_obj.repr = anon_repr;
    anonident_obj.calc2 = calc2;
    anonident_obj.rcalc2 = rcalc2;
}
