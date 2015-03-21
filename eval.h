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
#ifndef EVAL_H
#define EVAL_H
#include "obj.h"

struct file_s;
typedef struct List Colonlist;
typedef struct Label Label;
typedef struct Error Error;

extern int get_exp(int *, int, struct file_s *, unsigned int, unsigned int, linepos_t);
extern int get_exp_var(struct file_s *, linepos_t);
extern struct values_s *get_val(void);
extern Obj *pull_val(struct linepos_s *);
extern size_t get_val_remaining(void);
extern void destroy_eval(void);
extern void init_eval(void);
extern void eval_enter(void);
extern void eval_leave(void);
extern size_t get_label(void);
extern MUST_CHECK Obj *get_star_value(Obj *);
extern Obj *get_vals_tuple(void);
extern Obj *get_vals_addrlist(struct linepos_s *);
extern MUST_CHECK Error *indexoffs(Obj *, size_t, size_t *, linepos_t);
extern MUST_CHECK Obj *sliceparams(const Colonlist *, size_t, size_t *, ival_t *, ival_t *, ival_t *, linepos_t);
extern void touch_label(Label *);

struct values_s {
    Obj *val;
    struct linepos_s epoint;
};
#endif
