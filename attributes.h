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
#ifndef ATTRIBUTES_H
#define ATTRIBUTES_H

#ifndef __has_attribute
#define __has_attribute(a) __has_attribute##a
#define __has_attribute__unused__ (defined __GNUC__ && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)))
#define __has_attribute__warn_unused_result__ (defined __GNUC__ && __GNUC__ >= 4)
#define __has_attribute__malloc__ (defined __GNUC__ && __GNUC__ >= 3)
#define __has_attribute__noreturn__ (defined __GNUC__ && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)))
#define __has_attribute__regparm__ (defined __GNUC__ && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)))
#define __has_attribute__noinline__ (defined __GNUC__ && __GNUC__ >= 3)
#define __has_attribute__fallthrough__ (defined __GNUC__ && __GNUC__ >= 7)
#endif

#ifdef UNUSED
#elif __has_attribute(__warn_unused_result__)
# define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
# define UNUSED(x) x
#endif

#ifdef MUST_CHECK
#elif __has_attribute(__warn_unused_result__)
# define MUST_CHECK __attribute__((__warn_unused_result__))
#else
# define MUST_CHECK
#endif

#ifdef MALLOC
#elif __has_attribute(__malloc__)
# if __has_attribute(__warn_unused_result__)
#  define MALLOC __attribute__((__malloc__,__warn_unused_result__))
# else
#  define MALLOC __attribute__((__malloc__))
# endif
#elif __has_attribute(__warn_unused_result__)
#  define MALLOC __attribute__((__warn_unused_result__))
#else
# define MALLOC
#endif

#ifdef NO_RETURN
#elif __has_attribute(__noreturn__)
# define NO_RETURN  __attribute__((__noreturn__))
#else
# define NO_RETURN
#endif

#ifdef FAST_CALL
#elif __has_attribute(__regparm__) && defined __i386__
# define FAST_CALL  __attribute__((__regparm__(3)))
#else
# define FAST_CALL
#endif

#ifdef NO_INLINE
#elif __has_attribute(__noinline__)
# define NO_INLINE  __attribute__((__noinline__))
#else
# define NO_INLINE
#endif

#ifndef __cplusplus
#if __STDC_VERSION__ >= 199901L
#elif defined(__GNUC__)
# define inline __inline
#elif _MSC_VER >= 900
# define inline __inline
#else
# define inline
#endif
#endif

#ifdef FALL_THROUGH
#elif __has_attribute(__fallthrough__)
# define FALL_THROUGH __attribute__((__fallthrough__))
#else
# define FALL_THROUGH do {} while (false)
#endif

#endif
