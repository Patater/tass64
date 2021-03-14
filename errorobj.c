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
#include "errorobj.h"
#include "eval.h"
#include "values.h"
#include "error.h"
#include "64tass.h"
#include "file.h"
#include "macro.h"

#include "typeobj.h"
#include "registerobj.h"
#include "namespaceobj.h"

static Type obj;

Type *const ERROR_OBJ = &obj;

static FAST_CALL void destroy(Obj *o1) {
    Error *v1 = Error(o1);
    if (v1->line != NULL) free((uint8_t *)v1->line);
    switch (v1->num) {
    case ERROR__INVALID_OPER:
        if (v1->u.invoper.v1 != NULL) val_destroy(v1->u.invoper.v1);
        if (v1->u.invoper.v2 != NULL) val_destroy(v1->u.invoper.v2);
        return;
    case ERROR___NO_REGISTER:
        val_destroy(Obj(v1->u.reg.reg));
        return;
    case ERROR_____CANT_UVAL:
    case ERROR_____CANT_IVAL:
    case ERROR______NOT_UVAL:
        val_destroy(v1->u.intconv.val);
        return;
    case ERROR___NOT_DEFINED:
        val_destroy(v1->u.notdef.symbol);
        val_destroy(Obj(v1->u.notdef.names));
        return;
    case ERROR__NOT_KEYVALUE:
    case ERROR__NOT_HASHABLE:
    case ERROR_____CANT_SIGN:
    case ERROR______CANT_ABS:
    case ERROR______CANT_INT:
    case ERROR______CANT_LEN:
    case ERROR_____CANT_SIZE:
    case ERROR_____CANT_BOOL:
    case ERROR______NOT_ITER:
    case ERROR___MATH_DOMAIN:
    case ERROR_LOG_NON_POSIT:
    case ERROR_SQUARE_ROOT_N:
    case ERROR___INDEX_RANGE:
    case ERROR_____KEY_ERROR:
    case ERROR_DIVISION_BY_Z:
    case ERROR_ZERO_NEGPOWER:
        val_destroy(v1->u.obj);
        return;
    case ERROR__INVALID_CONV:
        val_destroy(v1->u.conv.val);
        return;
    default: return;
    }
}

static FAST_CALL void garbage(Obj *o1, int i) {
    Error *v1 = Error(o1);
    Obj *v;
    switch (v1->num) {
    case ERROR__INVALID_OPER:
        v = v1->u.invoper.v1;
        if (v != NULL && v1->u.invoper.v2 != NULL) {
            switch (i) {
            case -1:
                v->refcount--;
                break;
            case 0:
                break;
            case 1:
                if ((v->refcount & SIZE_MSB) != 0) {
                    v->refcount -= SIZE_MSB - 1;
                    v->obj->garbage(v, 1);
                } else v->refcount++;
                break;
            }
            v = v1->u.invoper.v2;
        }
        break;
    case ERROR___NO_REGISTER:
        v = Obj(v1->u.reg.reg);
        break;
    case ERROR_____CANT_UVAL:
    case ERROR_____CANT_IVAL:
    case ERROR______NOT_UVAL:
        v = v1->u.intconv.val;
        break;
    case ERROR___NOT_DEFINED:
        v = v1->u.notdef.symbol;
        switch (i) {
        case -1:
            v->refcount--;
            break;
        case 0:
            break;
        case 1:
            if ((v->refcount & SIZE_MSB) != 0) {
                v->refcount -= SIZE_MSB - 1;
                v->obj->garbage(v, 1);
            } else v->refcount++;
            break;
        }
        v = Obj(v1->u.notdef.names);
        break;
    case ERROR__NOT_KEYVALUE:
    case ERROR__NOT_HASHABLE:
    case ERROR_____CANT_SIGN:
    case ERROR______CANT_ABS:
    case ERROR______CANT_INT:
    case ERROR______CANT_LEN:
    case ERROR_____CANT_SIZE:
    case ERROR_____CANT_BOOL:
    case ERROR______NOT_ITER:
    case ERROR___MATH_DOMAIN:
    case ERROR_LOG_NON_POSIT:
    case ERROR_SQUARE_ROOT_N:
    case ERROR___INDEX_RANGE:
    case ERROR_____KEY_ERROR:
    case ERROR_DIVISION_BY_Z:
    case ERROR_ZERO_NEGPOWER:
        v = v1->u.obj;
        break;
    case ERROR__INVALID_CONV:
        v = v1->u.conv.val;
        break;
    default:
        if (i == 0) break;
        return;
    }
    switch (i) {
    case -1:
        v->refcount--;
        return;
    case 0:
        if (v1->line != NULL) free((uint8_t *)v1->line);
        return;
    case 1:
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

MALLOC Error *new_error(Error_types num, linepos_t epoint) {
    Error *v = Error(val_alloc(ERROR_OBJ));
    v->num = num;
    v->file_list = current_file_list;
    v->epoint.line = epoint->line;
    v->caret = epoint->pos;
    v->epoint.pos = macro_error_translate2(epoint->pos);
    if ((size_t)(pline - current_file_list->file->data) >= current_file_list->file->len) {
        size_t ln = strlen((const char *)pline) + 1;
        uint8_t *l = (uint8_t *)malloc(ln);
        if (l != NULL) memcpy(l, pline, ln);
        v->line = l;
    } else v->line = NULL;
    return v;
}

MALLOC Obj *new_error_mem(linepos_t epoint) {
    return Obj(new_error(ERROR_OUT_OF_MEMORY, epoint));
}

MALLOC Obj *new_error_obj(Error_types num, Obj *v1, linepos_t epoint) {
    Error *v = new_error(num, epoint);
    v->u.obj = val_reference(v1);
    return Obj(v);
}

MALLOC Obj *new_error_conv(Obj *v1, Type *t, linepos_t epoint) {
    Error *v = new_error(ERROR__INVALID_CONV, epoint);
    v->u.conv.t = t;
    v->u.conv.val = val_reference(v1);
    return Obj(v);
}

MALLOC Obj *new_error_argnum(argcount_t num, argcount_t min, argcount_t max, linepos_t epoint) {
    Error *v = new_error(ERROR__WRONG_ARGNUM, epoint);
    v->u.argnum.num = num;
    v->u.argnum.min = min;
    v->u.argnum.max = max;
    return Obj(v);
}

static MUST_CHECK Obj *calc1(oper_t op) {
    return val_reference(op->v1);
}

static MUST_CHECK Obj *calc2(oper_t op) {
    return val_reference(op->v1);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    return val_reference(op->v2);
}

static MUST_CHECK Obj *slice(oper_t op, argcount_t UNUSED(indx)) {
    return val_reference(op->v1);
}

void errorobj_init(void) {
    new_type(&obj, T_ERROR, "error", sizeof(Error));
    obj.destroy = destroy;
    obj.garbage = garbage;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
    obj.slice = slice;
}

void error_obj_update(Error *err, const Obj *v1, Obj *v2) {
    switch (err->num) {
    case ERROR__INVALID_OPER:
        if (err->u.invoper.v1 == v1) {
            val_replace(&err->u.invoper.v1, v2);
        }
        if (err->u.invoper.v2 == v1) {
            val_replace(&err->u.invoper.v2, v2);
        }
        return;
    case ERROR_____CANT_UVAL:
    case ERROR_____CANT_IVAL:
    case ERROR______NOT_UVAL:
        if (err->u.intconv.val == v1) {
            val_replace(&err->u.intconv.val, v2);
        }
        return;
    case ERROR__NOT_KEYVALUE:
    case ERROR__NOT_HASHABLE:
    case ERROR_____CANT_SIGN:
    case ERROR______CANT_ABS:
    case ERROR______CANT_INT:
    case ERROR______CANT_LEN:
    case ERROR_____CANT_SIZE:
    case ERROR_____CANT_BOOL:
    case ERROR______NOT_ITER:
    case ERROR___MATH_DOMAIN:
    case ERROR_LOG_NON_POSIT:
    case ERROR_SQUARE_ROOT_N:
    case ERROR___INDEX_RANGE:
    case ERROR_____KEY_ERROR:
    case ERROR_DIVISION_BY_Z:
    case ERROR_ZERO_NEGPOWER:
        if (err->u.obj == v1) {
            val_replace(&err->u.obj, v2);
        }
        return;
    default:
        return;
    }
}
