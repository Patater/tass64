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
#include "values.h"
#include "obj.h"

extern int get_exp(int *, int, struct file_s *);
extern int get_exp_var(struct file_s *);
extern struct value_s *get_val(struct linepos_s *);
extern void destroy_eval(void);
extern void init_eval(void);
extern void eval_enter(void);
extern void eval_leave(void);
extern int eval_finish(void);
extern size_t get_label(void);
extern struct value_s *get_vals_tuple(void);
extern struct value_s *get_vals_addrlist(struct linepos_s *);
extern ival_t indexoffs(const struct value_s *, size_t);
extern void touch_label(struct label_s *);

struct values_s {
    struct value_s *val;
    struct linepos_s epoint;
};
#endif
