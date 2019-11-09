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
#include "dictobj.h"
#include <string.h>
#include "eval.h"
#include "error.h"
#include "variables.h"

#include "intobj.h"
#include "listobj.h"
#include "strobj.h"
#include "boolobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"
#include "iterobj.h"

static Type obj;

Type *const DICT_OBJ = &obj;

static struct pair_s *allocate(size_t ln) {
    struct pair_s *p;
    size_t ln2;
    if (ln > SIZE_MAX / (sizeof(struct pair_s) + sizeof(size_t) * 2)) return NULL; /* overflow */
    ln2 = ln * 3 / 2 * sizeof(size_t);
    p = (struct pair_s *)malloc(ln * sizeof(struct pair_s) + ln2);
    memset(&p[ln], 255, ln2);
    return p;
}

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_DICT: return val_reference(v1);
    default: break;
    }
    return (Obj *)new_error_conv(v1, DICT_OBJ, epoint);
}

static FAST_CALL void destroy(Obj *o1) {
    Dict *v1 = (Dict *)o1;
    size_t i;
    for (i = 0; i < v1->len; i++) {
        struct pair_s *a = &v1->data[i];
        val_destroy(a->key);
        if (a->data != NULL) val_destroy(a->data);
    }
    free(v1->data);
    if (v1->def != NULL) val_destroy(v1->def);
}

static FAST_CALL void garbage(Obj *o1, int j) {
    Dict *v1 = (Dict *)o1;
    Obj *v;
    size_t i;
    switch (j) {
    case -1:
        for (i = 0; i < v1->len; i++) {
            struct pair_s *a = &v1->data[i];
            a->key->refcount--;
            if (a->data != NULL) a->data->refcount--;
        }
        v = v1->def;
        if (v != NULL) v->refcount--;
        return;
    case 0:
        free(v1->data);
        return;
    case 1:
        for (i = 0; i < v1->len; i++) {
            struct pair_s *a = &v1->data[i];
            v = a->data;
            if (v != NULL) {
                if ((v->refcount & SIZE_MSB) != 0) {
                    v->refcount -= SIZE_MSB - 1;
                    v->obj->garbage(v, 1);
                } else v->refcount++;
            }
            v = a->key;
            if ((v->refcount & SIZE_MSB) != 0) {
                v->refcount -= SIZE_MSB - 1;
                v->obj->garbage(v, 1);
            } else v->refcount++;
        }
        v = v1->def;
        if (v == NULL) return;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

static struct oper_s pair_oper;

static int rpair_compare(Obj *o1, Obj *o2) {
    int h;
    Obj *result;
    Iter *iter1 = o1->obj->getiter(o1);
    Iter *iter2 = o2->obj->getiter(o2);
    iter_next_t iter1_next = iter1->next;
    iter_next_t iter2_next = iter2->next;
    do {
        o1 = iter1_next(iter1);
        o2 = iter2_next(iter2);
        if (o1 == NULL) {
            h = (o2 == NULL) ? 0 : -1;
            break;
        }
        if (o2 == NULL) {
            h = 1;
            break;
        }
        if (o1->obj->iterable || o2->obj->iterable) {
            if (o1->obj->iterable && o2->obj->iterable) {
                h = rpair_compare(o1, o2);
            } else {
                h = o1->obj->type - o2->obj->type;
            }
        } else {
            pair_oper.v1 = o1;
            pair_oper.v2 = o2;
            pair_oper.inplace = NULL;
            result = o1->obj->calc2(&pair_oper);
            if (result->obj == INT_OBJ) h = (int)((Int *)result)->len;
            else h = o1->obj->type - o2->obj->type;
            val_destroy(result);
        }
    } while (h == 0);
    val_destroy(&iter2->v);
    val_destroy(&iter1->v);
    return h;
}

static int pair_compare(const struct pair_s *a, const struct pair_s *b)
{
    Obj *result;
    int h;
    if (a->key->obj == b->key->obj) {
        h = a->hash - b->hash;
        if (h != 0) return h;
    }
    if (a->key->obj->iterable || b->key->obj->iterable) {
        if (a->key->obj->iterable && b->key->obj->iterable) {
            return rpair_compare(a->key, b->key);
        }
        return a->key->obj->type - b->key->obj->type;
    }
    pair_oper.v1 = a->key;
    pair_oper.v2 = b->key;
    pair_oper.inplace = NULL;
    result = pair_oper.v1->obj->calc2(&pair_oper);
    if (result->obj == INT_OBJ) h = (int)((Int *)result)->len;
    else h = a->key->obj->type - b->key->obj->type;
    val_destroy(result);
    return h;
}

static size_t *lookup(const Dict *dict, const struct pair_s *p) {
    struct pair_s *d;
    size_t *indexes = (size_t *)&dict->data[dict->max];
    size_t hashlen = dict->max * 3 / 2;
    size_t hash = (size_t)p->hash;
    size_t offs = hash % hashlen;
    while (indexes[offs] != SIZE_MAX) {
        d = &dict->data[indexes[offs]];
        if (p->key == d->key || pair_compare(p, d) == 0) return &indexes[offs];
        offs++;
        if (offs == hashlen) offs = 0;
    } 
    return &indexes[offs];
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Dict *v1 = (const Dict *)o1, *v2 = (const Dict *)o2;
    size_t n;
    if (o2->obj != DICT_OBJ || v1->len != v2->len) return false;
    if (v1->def != v2->def) {
        if (v1->def == NULL || v2->def == NULL) return false;
        if (!v1->def->obj->same(v1->def, v2->def)) return false;
    }
    for (n = 0; n < v1->len; n++) {
        const struct pair_s *p = &v1->data[n], *p2;
        size_t n2 = *lookup(v2, p);
        if (n2 == SIZE_MAX) return false;
        p2 = &v2->data[n2];
        if (p->key != p2->key && !p->key->obj->same(p->key, p2->key)) return false;
        if (p->data == p2->data) continue;
        if (p->data == NULL || p2->data == NULL) return false;
        if (!p->data->obj->same(p->data, p2->data)) return false;
    }
    return true;
}

static MUST_CHECK Obj *len(Obj *o1, linepos_t UNUSED(epoint)) {
    Dict *v1 = (Dict *)o1;
    return (Obj *)int_from_size(v1->len);
}

static FAST_CALL MUST_CHECK Obj *next(Iter *v1) {
    Colonlist *iter;
    const Dict *vv1 = (Dict *)v1->data;
    if (v1->val >= vv1->len) return NULL;
    if (vv1->data[v1->val].data == NULL) {
        return vv1->data[v1->val++].key;
    }
    iter = (Colonlist *)v1->iter;
    if (iter == NULL) {
    renew:
        iter = new_colonlist();
        v1->iter = &iter->v;
    } else if (iter->v.refcount != 1) {
        iter->v.refcount--;
        goto renew;
    } else {
        val_destroy(iter->data[0]);
        val_destroy(iter->data[1]);
    }
    iter->len = 2;
    iter->data = iter->u.val;
    iter->data[0] = val_reference(vv1->data[v1->val].key);
    iter->data[1] = val_reference(vv1->data[v1->val++].data);
    return &iter->v;
}

static MUST_CHECK Iter *getiter(Obj *v1) {
    Iter *v = (Iter *)val_alloc(ITER_OBJ);
    v->iter = NULL;
    v->val = 0;
    v->data = val_reference(v1);
    v->next = next;
    v->len = ((Dict *)v1)->len;
    return v;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    Dict *v1 = (Dict *)o1;
    const struct pair_s *p;
    size_t i = 0, j, ln = 2, chars = 2;
    Tuple *list = NULL;
    Obj **vals;
    Obj *v;
    Str *str;
    uint8_t *s;
    size_t def = (v1->def != NULL) ? 1 : 0;
    if (v1->len != 0 || def != 0) {
        ln = v1->len * 2;
        if (ln < v1->len) return NULL; /* overflow */
        ln += def;
        if (ln < def) return NULL; /* overflow */
        chars = ln + 1 + def;
        if (chars < ln) return NULL; /* overflow */
        if (chars > maxsize) return NULL;
        list = new_tuple(ln);
        vals = list->data;
        ln = chars;
        if (v1->len != 0) {
            size_t n;
            for (n = 0; n < v1->len; n++) {
                p = &v1->data[n];
                v = p->key->obj->repr(p->key, epoint, maxsize - chars);
                if (v == NULL || v->obj != STR_OBJ) goto error;
                str = (Str *)v;
                ln += str->len;
                if (ln < str->len) goto error2; /* overflow */
                chars += str->chars;
                if (chars > maxsize) goto error2;
                vals[i++] = v;
                if (p->data != NULL) {
                    v = p->data->obj->repr(p->data, epoint, maxsize - chars);
                    if (v == NULL || v->obj != STR_OBJ) goto error;
                    str = (Str *)v;
                    ln += str->len;
                    if (ln < str->len) goto error2; /* overflow */
                    chars += str->chars;
                    if (chars > maxsize) goto error2;
                } else {
                    v = (Obj *)ref_none();
                    ln--;
                    chars--;
                }
                vals[i++] = v;
            }
        }
        if (def != 0) {
            v = v1->def->obj->repr(v1->def, epoint, maxsize - chars);
            if (v == NULL || v->obj != STR_OBJ) goto error;
            str = (Str *)v;
            ln += str->len;
            if (ln < str->len) goto error2; /* overflow */
            chars += str->chars;
            if (chars > maxsize) {
            error2:
                val_destroy(v);
                v = NULL;
            error:
                list->len = i;
                val_destroy(&list->v);
                return v;
            }
            vals[i] = v;
        }
        list->len = i + def;
    }
    str = new_str2(ln);
    if (str == NULL) {
        if (list != NULL) val_destroy(&list->v);
        return NULL;
    }
    str->chars = chars;
    s = str->data;
    *s++ = '{';
    for (j = 0; j < i; j++) {
        Str *str2 = (Str *)vals[j];
        if (str2->v.obj != STR_OBJ) continue;
        if (j != 0) *s++ = ((j & 1) != 0) ? ':' : ',';
        if (str2->len != 0) {
            memcpy(s, str2->data, str2->len);
            s += str2->len;
        }
    }
    if (def != 0) {
        Str *str2 = (Str *)vals[j];
        if (j != 0) *s++ = ',';
        *s++ = ':';
        if (str2->len != 0) {
            memcpy(s, str2->data, str2->len);
            s += str2->len;
        }
    }
    *s = '}';
    if (list != NULL) val_destroy(&list->v);
    return &str->v;
}

static MUST_CHECK Obj *findit(Dict *v1, Obj *o2, linepos_t epoint) {
    if (v1->len != 0) {
        Error *err;
        size_t b;
        struct pair_s pair;
        pair.key = o2;
        err = o2->obj->hash(o2, &pair.hash, epoint);
        if (err != NULL) return &err->v;
        b = *lookup(v1, &pair);
        if (b != SIZE_MAX) {
            const struct pair_s *p = &v1->data[b];
            if (p->data != NULL) {
                return val_reference(p->data);
            }
        }
    }
    if (v1->def != NULL) {
        return val_reference(v1->def);
    }
    return (Obj *)new_error_obj(ERROR_____KEY_ERROR, o2, epoint);
}

static MUST_CHECK Obj *slice(Obj *o1, oper_t op, size_t indx) {
    Obj *o2 = op->v2, *vv;
    Dict *v1 = (Dict *)o1;
    Funcargs *args = (Funcargs *)o2;
    bool more = args->len > indx + 1;
    linepos_t epoint2;

    if (args->len < 1) {
        err_msg_argnum(args->len, 1, 0, op->epoint2);
        return (Obj *)ref_none();
    }

    o2 = args->val[indx].val;
    epoint2 = &args->val[indx].epoint;

    if (o2 == &none_value->v) return val_reference(o2);
    if (o2->obj->iterable) {
        iter_next_t iter_next;
        Iter *iter = o2->obj->getiter(o2);
        size_t i, len2 = iter->len;
        List *v;
        Obj **vals;

        if (len2 == 0) {
            val_destroy(&iter->v);
            return val_reference(&null_list->v);
        }
        v = new_list();
        v->data = vals = list_create_elements(v, len2);
        pair_oper.epoint3 = epoint2;
        iter_next = iter->next;
        for (i = 0; i < len2 && (o2 = iter_next(iter)) != NULL; i++) {
            vv = findit(v1, o2, epoint2);
            if (vv->obj != ERROR_OBJ && more) vv = vv->obj->slice(vv, op, indx + 1);
            vals[i] = vv;
        }
        val_destroy(&iter->v);
        v->len = i;
        return &v->v;
    }

    pair_oper.epoint3 = epoint2;
    vv = findit(v1, o2, epoint2);
    if (vv->obj != ERROR_OBJ && more) vv = vv->obj->slice(vv, op, indx + 1);
    return vv;
}

MUST_CHECK Obj *dict_sort(Dict *v1, const size_t *sort_index) {
    size_t i;
    Dict *v = (Dict *)val_alloc(DICT_OBJ);
    v->len = v1->len;
    v->max = v1->len;
    v->data = allocate(v1->len);
    if (v->data == NULL) err_msg_out_of_memory(); /* overflow */
    for (i = 0; i < v1->len; i++) {
        struct pair_s *s = &v1->data[sort_index[i]], *d = &v->data[i];
        d->hash = s->hash;
        d->key = val_reference(s->key);
        d->data = (s->data == NULL) ? NULL : val_reference(s->data);
        *lookup(v, d) = i;
    }
    v->def = (v1->def == NULL) ? NULL : val_reference(v1->def);
    return &v->v;
}

static MUST_CHECK Obj *concat(oper_t op) {
    Dict *v1 = (Dict *)op->v1;
    Dict *v2 = (Dict *)op->v2;
    unsigned int j;
    size_t ln;
    Dict *dict;

    if (v2->len == 0 && v2->def == NULL) return val_reference(&v1->v);
    if (v1->len == 0 && (v1->def == NULL || v2->def != NULL)) return val_reference(&v2->v);

    ln = v1->len + v2->len;
    dict = (Dict *)val_alloc(DICT_OBJ);
    dict->len = 0;
    dict->max = ln;
    dict->def = NULL;
    if (ln < v1->len) dict->data = NULL; /* overflow */
    else if (ln == 0) {
        dict->data = NULL;
        return &dict->v;
    } else dict->data = allocate(ln);
    if (dict->data == NULL) {
        val_destroy(&dict->v);
        return (Obj *)new_error_mem(op->epoint3);
    }

    for (j = 0; j < v1->len; j++) {
        Obj *data;
        struct pair_s *p = &dict->data[dict->len];
        p->hash = v1->data[j].hash;
        p->key = val_reference(v1->data[j].key);
        data = v1->data[j].data;
        p->data = (data == NULL) ? NULL : val_reference(data);
        *lookup(dict, p) = dict->len++;
    }
    dict->def = v2->def != NULL ? val_reference(v2->def) : v1->def != NULL ? val_reference(v1->def) : NULL;
    for (j = 0; j < v2->len; j++) {
        size_t *b;
        Obj *data;
        struct pair_s *p = &dict->data[dict->len];
        p->hash = v2->data[j].hash;
        p->key = v2->data[j].key;
        b = lookup(dict, p);
        if (*b != SIZE_MAX) {
            p = &dict->data[*b];
            if (p->data != NULL) val_destroy(p->data);
        } else {
            p->key = val_reference(p->key);
            *b = dict->len++;
        }
        data = v2->data[j].data;
        p->data = (data == NULL) ? NULL : val_reference(data);
    }
    if (dict->len == 0) {
        free(dict->data);
        dict->data = NULL;
    } else if (ln - dict->len > 2) {
        struct pair_s *v = (struct pair_s *)realloc(dict->data, dict->len * sizeof *dict->data);
        if (v != NULL) dict->data = v;
    }
    return &dict->v;
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *o2 = op->v2;

    switch (o2->obj->type) {
    case T_DICT:
        if (op->op->op == O_CONCAT) {
            return concat(op);
        }
        break;
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
    Dict *v2 = (Dict *)op->v2;
    Obj *o1 = op->v1;
    if (op->op == &o_IN) {
        struct pair_s p;
        Error *err;

        if (v2->len == 0) return val_reference(&false_value->v);
        p.key = o1;
        err = o1->obj->hash(o1, &p.hash, op->epoint);
        if (err != NULL) return &err->v;
        return truth_reference(*lookup(v2, &p) != SIZE_MAX);
    }
    switch (o1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_TUPLE:
    case T_LIST:
        return o1->obj->calc2(op);
    default: break;
    }
    return obj_oper_error(op);
}

Obj *dictobj_parse(struct values_s *values, size_t args) {
    unsigned int j;
    Dict *dict = (Dict *)val_alloc(DICT_OBJ);
    dict->len = 0;
    dict->max = args;
    dict->def = NULL;
    if (args == 0) {
        dict->data = NULL;
        return &dict->v;
    }
    dict->data = allocate(args);
    if (dict->data == NULL) {
        val_destroy(&dict->v);
        return (Obj *)new_error_mem(&values->epoint);
    }

    for (j = 0; j < args; j++) {
        Obj *data;
        Error *err;
        struct pair_s *p;
        size_t *b;
        struct values_s *v2 = &values[j];
        Obj *key = v2->val;

        if (key == &none_value->v || key->obj == ERROR_OBJ) {
            val_destroy(&dict->v);
            return val_reference(key);
        }
        if (key->obj != COLONLIST_OBJ) data = NULL;
        else {
            Colonlist *list = (Colonlist *)key;
            if (list->len != 2 || list->data[1] == &default_value->v) {
                err = new_error(ERROR__NOT_KEYVALUE, &v2->epoint);
                err->u.obj = val_reference(key);
                val_destroy(&dict->v);
                return &err->v;
            }
            key = list->data[0];
            data = list->data[1];
        }
        if (key == &default_value->v) {
            if (dict->def != NULL) val_destroy(dict->def);
            dict->def = (data == NULL) ? NULL : val_reference(data);
            continue;
        }
        p = &dict->data[dict->len];
        err = key->obj->hash(key, &p->hash, &v2->epoint);
        if (err != NULL) {
            val_destroy(&dict->v);
            return &err->v;
        }
        p->key = key;
        b = lookup(dict, p);
        if (*b != SIZE_MAX) {
            p = &dict->data[*b];
            if (p->data != NULL) val_destroy(p->data);
        } else {
            if (data == NULL) {
                v2->val = NULL;
            } else {
                p->key = val_reference(p->key);
            }
            *b = dict->len++;
        }
        p->data = (data == NULL) ? NULL : val_reference(data);
    }
    if (dict->len == 0) {
        free(dict->data);
        dict->data = NULL;
    } else if (args - dict->len > 2) {
        struct pair_s *v = (struct pair_s *)realloc(dict->data, dict->len * sizeof *dict->data);
        if (v != NULL) dict->data = v;
    }
    return &dict->v;
}

void dictobj_init(void) {
    static struct linepos_s nopoint;

    new_type(&obj, T_DICT, "dict", sizeof(Dict));
    obj.iterable = true;
    obj.create = create;
    obj.destroy = destroy;
    obj.garbage = garbage;
    obj.same = same;
    obj.len = len;
    obj.getiter = getiter;
    obj.repr = repr;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
    obj.slice = slice;

    pair_oper.op = &o_CMP;
    pair_oper.epoint = &nopoint;
    pair_oper.epoint2 = &nopoint;
    pair_oper.epoint3 = &nopoint;
}

void dictobj_names(void) {
    new_builtin("dict", val_reference(&DICT_OBJ->v));
}

