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
#ifndef NONEOBJ_H
#define NONEOBJ_H
#include "obj.h"

extern struct Type *const NONE_OBJ;

typedef struct None {
    Obj v;
} None;

#define None(a) OBJ_CAST(None, a)

extern Obj *const none_value;

extern void noneobj_init(void);
extern void noneobj_destroy(void);

static inline Obj *ref_none(void) {
    none_value->refcount++; return none_value;
}

#endif
