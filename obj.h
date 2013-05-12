/*

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/
#ifndef _OBJ_H
#define _OBJ_H
#include <stdio.h>
#include "inttypes.h"
struct value_s;
struct oper_s;
struct values_s;
enum type_e;
enum atype_e;
enum oper_e;

#define obj_print(v, f) do { const struct value_s *_v_ = (v); _v_->obj->print(_v_, f); } while (0)
#define obj_destroy(v) do { struct value_s *_v_ = (v); _v_->obj->destroy(_v_); } while (0)
#define obj_same(v, v2) v->obj->same(v, v2)
#define obj_truth(v) v->obj->truth(v)
#define obj_hash(v, v2, epoint) v->obj->hash(v, v2, epoint)

typedef const struct obj_s* obj_t;
typedef struct oper_s* oper_t;
struct obj_s {
    int type;
    const char *name;
    void (*destroy)(struct value_s *);
    void (*copy)(const struct value_s *, struct value_s *);
    void (*copy_temp)(const struct value_s *, struct value_s *);
    int (*same)(const struct value_s *, const struct value_s *);
    int (*truth)(const struct value_s *);
    int (*hash)(const struct value_s *, struct value_s *, linepos_t);
    void (*convert)(struct value_s *, struct value_s *, obj_t, linepos_t, linepos_t);
    void (*calc1)(oper_t);
    void (*calc2)(oper_t);
    void (*rcalc2)(oper_t);
    void (*repeat)(oper_t, uval_t);
    void (*print)(const struct value_s *, FILE *);
    void (*iindex)(oper_t);
    void (*slice)(struct value_s *, ival_t, ival_t, ival_t, struct value_s *, linepos_t);
};

extern void obj_init(struct obj_s *, enum type_e, const char *);
extern void obj_oper_error(oper_t);
extern void objects_init(void);

extern obj_t LBL_OBJ;
extern obj_t MACRO_OBJ;
extern obj_t SEGMENT_OBJ;
extern obj_t FUNCTION_OBJ;
extern obj_t STRUCT_OBJ;
extern obj_t UNION_OBJ;
extern obj_t IDENTREF_OBJ;
extern obj_t NONE_OBJ;
extern obj_t ERROR_OBJ;
extern obj_t GAP_OBJ;
extern obj_t IDENT_OBJ;
extern obj_t ANONIDENT_OBJ;
extern obj_t OPER_OBJ;
extern obj_t DEFAULT_OBJ;
extern obj_t DICT_OBJ;
extern obj_t PAIR_OBJ;

extern int referenceit;
#endif
