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
#include "values.h"
#include <string.h>
#include "obj.h"

#include "unicode.h"
#include "error.h"
#include "strobj.h"
#include "typeobj.h"

#define SLOTS 128
#define ALIGN sizeof(int *)

typedef struct Slot {
    Obj v;
    struct Slot *next;
} Slot;

typedef struct Slotcoll {
    struct Slotcoll *next;
} Slotcoll;

static Slotcoll *slotcoll[MAXIMUM_TYPE_LENGTH];

static void value_free(Obj *val) {
    Slot *slot = (Slot *)val, **c = val->obj->slot;
    slot->next = *c;
    val->obj = NULL;
    *c = slot;
}

static FAST_CALL NO_INLINE Obj *value_alloc(const Type *obj) {
    size_t p = obj->length;
    size_t size = p * ALIGN;
    Slot *slot, *slot2;
    Slotcoll **s = &slotcoll[p];
    size_t i = size * SLOTS + sizeof(Slotcoll);
    Slotcoll *n = (Slotcoll *)allocate_array(uint8_t, i);
    if (n == NULL) err_msg_out_of_memory();
    n->next = *s; *s = n;
    slot2 = slot = (Slot *)(n + 1);
    for (i = 0; i < (SLOTS - 1); i++, slot2 = slot2->next) {
        slot2->v.obj = NULL;
        slot2->v.refcount = 1;
        slot2->next = (Slot *)(((char *)slot2) + size);
    }
    slot2->v.obj = NULL;
    slot2->v.refcount = 1;
    slot2->next = NULL;

    slot->v.obj = obj;
    *obj->slot = slot->next;
    return Obj(slot);
}

FAST_CALL MUST_CHECK Obj *val_alloc(const Type *obj) {
    Slot *slot = *obj->slot;
    if (slot == NULL) return value_alloc(obj);
    slot->v.obj = obj;
    *obj->slot = slot->next;
    return Obj(slot);
}

void garbage_collect(void) {
    Slotcoll *vals;
    size_t i, j;

    for (j = 0; j < lenof(slotcoll); j++) {
        size_t size = j * ALIGN;
        for (vals = slotcoll[j]; vals != NULL; vals = vals->next) {
            Obj *val = (Obj *)(vals + 1);
            for (i = 0; i < SLOTS; i++, val = (Obj *)(((char *)val) + size)) {
                if (val->obj != NULL && val->obj->garbage != NULL) {
                    val->obj->garbage(val, -1);
                    val->refcount |= SIZE_MSB;
                }
            }
        }
    }

    for (j = 0; j < lenof(slotcoll); j++) {
        size_t size = j * ALIGN;
        for (vals = slotcoll[j]; vals != NULL; vals = vals->next) {
            Obj *val = (Obj *)(vals + 1);
            for (i = 0; i < SLOTS; i++, val = (Obj *)(((char *)val) + size)) {
                if (val->obj != NULL && val->obj->garbage != NULL) {
                    if (val->refcount > SIZE_MSB) {
                        val->refcount -= SIZE_MSB;
                        val->obj->garbage(val, 1);
                    }
                }
            }
        }
    }

    for (j = 0; j < lenof(slotcoll); j++) {
        size_t size = j * ALIGN;
        for (vals = slotcoll[j]; vals != NULL; vals = vals->next) {
            Obj *val = (Obj *)(vals + 1);
            for (i = 0; i < SLOTS; i++, val = (Obj *)(((char *)val) + size)) {
                if ((val->refcount & ~SIZE_MSB) == 0) {
                    val->refcount = 1;
                    if (val->obj->garbage != NULL) val->obj->garbage(val, 0);
                    else if (val->obj->destroy != NULL) val->obj->destroy(val);
                    value_free(val);
                }
            }
        }
    }
}

FAST_CALL void val_destroy(Obj *val) {
    switch (val->refcount) {
    case 1:
        if (val->obj->destroy != NULL) val->obj->destroy(val);
        value_free(val);
        return;
    case 0:
        return;
    default:
        val->refcount--;
    }
}

FAST_CALL void val_replace(Obj **val, Obj *val2) {
    if (*val == val2) return;
    val_destroy(*val);
    *val = val_reference(val2);
}

size_t val_print(Obj *v1, FILE *f, size_t max) {
    size_t len;
    Obj *err = v1->obj->repr(v1, NULL, max);
    if (err == NULL) return 0;
    if (err->obj == STR_OBJ) len = printable_print2(Str(err)->data, f, Str(err)->len);
    else len = printable_print2((const uint8_t *)err->obj->name, f, strlen(err->obj->name));
    val_destroy(err);
    return len;
}

FAST_CALL void iter_destroy(struct iter_s *v1) {
    if (v1->iter != NULL) val_destroy(v1->iter);
    val_destroy(v1->data);
}

void destroy_values(void)
{
    size_t j;
    garbage_collect();
    objects_destroy();

#ifdef DEBUG
    {
    Slotcoll *vals;
    size_t i;
    for (j = 0; j < lenof(slotcoll); j++) {
        size_t size = j * ALIGN;
        for (vals = slotcoll[j]; vals; vals = vals->next) {
            Obj *val = (Obj *)(vals + 1);
            for (i = 0; i < SLOTS; i++, val = (Obj *)(((char *)val) + size)) {
                if (val->obj != NULL) {
                    val_print(val, stderr, SIZE_MAX);
                    fprintf(stderr, " %s %" PRIuSIZE " %" PRIxPTR "\n", val->obj->name, val->refcount, (uintptr_t)val);
                }
            }
        }
    }
    }
#endif

    for (j = 0; j < lenof(slotcoll); j++) {
        Slotcoll *s = slotcoll[j];
        if (s == NULL) continue;
        slotcoll[j] = NULL;
        do {
            Slotcoll *old = s;
            s = s->next;
            free(old);
        } while (s != NULL);
    }
}
