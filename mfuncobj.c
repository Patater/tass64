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
#include "mfuncobj.h"
#include "values.h"
#include "eval.h"
#include "error.h"
#include "macro.h"
#include "file.h"

#include "typeobj.h"
#include "namespaceobj.h"
#include "operobj.h"
#include "listobj.h"

static Type mfunc_obj;
static Type sfunc_obj;

Type *const MFUNC_OBJ = &mfunc_obj;
Type *const SFUNC_OBJ = &sfunc_obj;

static FAST_CALL void destroy(Obj *o1) {
    Mfunc *v1 = Mfunc(o1);
    const struct file_s *cfile = v1->file_list->file;
    size_t i = v1->argc;
    while ((i--) != 0) {
        struct mfunc_param_s *param = &v1->param[i];
        if ((size_t)(param->name.data - cfile->data) >= cfile->len) free((char *)param->name.data);
        if (param->name.data != param->cfname.data) free((char *)param->cfname.data);
        if (param->init != NULL) val_destroy(param->init);
    }
    i = v1->nslen;
    while ((i--) != 0) {
        val_destroy(&v1->namespaces[i]->v);
    }
    free(v1->namespaces);
    val_destroy(&v1->names->v);
    val_destroy(&v1->inamespaces->v);
    free(v1->param);
}

static FAST_CALL void garbage(Obj *o1, int j) {
    Mfunc *v1 = Mfunc(o1);
    size_t i = v1->argc;
    Obj *v ;
    const struct file_s *cfile;
    switch (j) {
    case -1:
        while ((i--) != 0) {
            v = v1->param[i].init;
            if (v != NULL) v->refcount--;
        }
        i = v1->nslen;
        while ((i--) != 0) {
            v = &v1->namespaces[i]->v;
            v->refcount--;
        }
        v = &v1->names->v;
        v->refcount--;
        v = &v1->inamespaces->v;
        v->refcount--;
        return;
    case 0:
        cfile = v1->file_list->file;
        while ((i--) != 0) {
            struct mfunc_param_s *param = &v1->param[i];
            if ((size_t)(param->name.data - cfile->data) >= cfile->len) free((char *)param->name.data);
            if (param->name.data != param->cfname.data) free((char *)param->cfname.data);
        }
        free(v1->param);
        free(v1->namespaces);
        return;
    case 1:
        while ((i--) != 0) {
            v = v1->param[i].init;
            if (v != NULL) {
                if ((v->refcount & SIZE_MSB) != 0) {
                    v->refcount -= SIZE_MSB - 1;
                    v->obj->garbage(v, 1);
                } else v->refcount++;
            }
        }
        i = v1->nslen;
        while ((i--) != 0) {
            v = &v1->namespaces[i]->v;
            if ((v->refcount & SIZE_MSB) != 0) {
                v->refcount -= SIZE_MSB - 1;
                v->obj->garbage(v, 1);
            } else v->refcount++;
        }
        v = &v1->names->v;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        v = &v1->inamespaces->v;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Mfunc *v1 = Mfunc(o1), *v2 = Mfunc(o2);
    size_t i;
    if (o1->obj != o2->obj || v1->file_list != v2->file_list || v1->epoint.line != v2->epoint.line || v1->epoint.pos != v2->epoint.pos) return false;
    if (v1->argc != v2->argc || v1->nslen != v2->nslen || v1->single != v2->single) return false;
    for (i = 0; i < v1->argc; i++) {
        if (str_cmp(&v1->param[i].name, &v2->param[i].name) != 0) return false;
        if ((v1->param[i].name.data != v1->param[i].cfname.data || v2->param[i].name.data != v2->param[i].cfname.data) && str_cmp(&v1->param[i].cfname, &v2->param[i].cfname) != 0) return false;
        if (v1->param[i].init != v2->param[i].init && (v1->param[i].init == NULL || v2->param[i].init == NULL || !v1->param[i].init->obj->same(v1->param[i].init, v2->param[i].init))) return false;
        if (v1->param[i].epoint.pos != v2->param[i].epoint.pos) return false;
    }
    for (i = 0; i < v1->nslen; i++) {
        if (v1->namespaces[i] != v2->namespaces[i] && !v1->namespaces[i]->v.obj->same(&v1->namespaces[i]->v, &v2->namespaces[i]->v)) return false;
    }
    if (v1->names != v2->names && !v1->names->v.obj->same(&v1->names->v, &v2->names->v)) return false;
    return true;
}

static MUST_CHECK Obj *calc2(oper_t op) {
    switch (op->v2->obj->type) {
    case T_FUNCARGS:
        switch (op->op->op) {
        case O_FUNC:
        {
            Mfunc *v1 = Mfunc(op->v1);
            Funcargs *v2 = Funcargs(op->v2);
            Obj *val;
            size_t i, max = 0, args = v2->len;
            for (i = args; i < v1->argc; i++) {
                if (v1->param[i].init == NULL) {
                    max = i + 1;
                }
            }
            if (max != 0) err_msg_argnum(args, max, v1->argc, op->epoint2);
            eval_enter();
            val = mfunc2_recurse(v1, v2->val, args, op->epoint);
            eval_leave();
            return (val != NULL) ? val : (Obj *)ref_tuple(null_tuple);
        }
        default: break;
        }
        break;
    case T_NONE:
    case T_ERROR:
        return val_reference(op->v2);
    default: 
        if (op->op == &o_MEMBER) {
            return namespace_member(op, Mfunc(op->v1)->names);
        }
        break;
    }
    return obj_oper_error(op);
}

void mfuncobj_init(void) {
    new_type(&mfunc_obj, T_MFUNC, "function", sizeof(Mfunc));
    mfunc_obj.destroy = destroy;
    mfunc_obj.garbage = garbage;
    mfunc_obj.same = same;
    mfunc_obj.calc2 = calc2;
    new_type(&sfunc_obj, T_SFUNC, "sfunction", sizeof(Sfunc));
    sfunc_obj.destroy = destroy;
    sfunc_obj.garbage = garbage;
    sfunc_obj.same = same;
    sfunc_obj.calc2 = calc2;
}

