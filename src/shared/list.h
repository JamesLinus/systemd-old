/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

#pragma once

/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include "macro.h"

/* The head of the linked list. Use this in the structure that shall
 * contain the head of the linked list */
#define LIST_HEAD(t,name)                                               \
        t *name

/* The pointers in the linked list's items. Use this in the item structure */
#define LIST_FIELDS(t,name)                                             \
        t *name##_next, *name##_prev, *name##_other_end

/* Initialize the list's head */
#define LIST_HEAD_INIT(head)                                            \
        do {                                                            \
                (head) = NULL;                                          \
        } while(false)

/* Initialize a list item */
#define LIST_INIT(name,item)                                            \
        do {                                                            \
                typeof(*(item)) *_item = (item);                        \
                assert(_item);                                          \
                _item->name##_prev = _item->name##_next = NULL;         \
                _item->name##_other_end = NULL;                         \
        } while(false)

#define LIST_NEXT(name, item)                                           \
        ((item)->name##_next)

#define LIST_PREV(name, item)                                           \
        ((item)->name##_prev)

#define LIST_EMPTY(head)                                                \
        (!(head))

/* Find the head of the list */
#define LIST_FIRST(name, item)                                          \
        ({                                                              \
                typeof(*(item)) *_i = (item);                           \
                if (_i && !_i->name##_next)                             \
                        _i = _i->name##_other_end;                      \
                else if (_i)                                            \
                        while (_i->name##_prev)                         \
                                _i = _i->name##_prev;                   \
                _i;                                                     \
        })

/* Find the tail of the list */
#define LIST_LAST(name, item)                                           \
        ({                                                              \
                typeof(*(item)) *_i = (item);                           \
                if (_i && !_i->name##_prev)                             \
                        _i = _i->name##_other_end;                      \
                else if (_i)                                            \
                        while (_i->name##_next)                         \
                                _i = _i->name##_next;                   \
                _i;                                                     \
        })

#define _LIST_INSERT_BETWEEN(name, _head, _a, _b, _item)                   \
                assert(_item);                                             \
                assert(!_a || LIST_NEXT(name, _a) == _b);                  \
                assert(!_b || LIST_PREV(name, _b) == _a);                  \
                                                                           \
                _item->name##_prev = _a;                                   \
                _item->name##_next = _b;                                   \
                if ((_a) == NULL && (_b) == NULL) {                        \
                        _item->name##_other_end = _item;                   \
                        *_head = _item;                                    \
                } else if (_a == NULL) {                                   \
                        _item->name##_other_end = _b->name##_other_end;    \
                        _b->name##_other_end = NULL;                       \
                        _item->name##_other_end->name##_other_end = _item; \
                        _b->name##_prev = _item;                           \
                        *_head = _item;                                    \
                } else if (_b == NULL) {                                   \
                        _item->name##_other_end = _a->name##_other_end;    \
                        _a->name##_other_end = NULL;                       \
                        _item->name##_other_end->name##_other_end = _item; \
                        _a->name##_next = _item;                           \
                } else {                                                   \
                        _a->name##_next = _item;                           \
                        _b->name##_prev = _item;                           \
                        _item->name##_other_end = NULL;                    \
                }

/* Prepend an item to the list */
#define LIST_PREPEND(name, head, item)                                  \
        LIST_INSERT_BEFORE(name, head, NULL, item)

#define LIST_APPEND(name, head, item)                                   \
        LIST_INSERT_AFTER(name, head, NULL, item)

/* Insert an item after another one */
#define LIST_INSERT_AFTER(name, head, where, item)                      \
        do {                                                            \
                typeof(*(head)) **_head = &(head), *_where = (where),   \
                         *_next, *_item = (item);                       \
                _where = _where ? _where : LIST_LAST(name, *_head);     \
                _next = _where ? LIST_NEXT(name, _where) : NULL;        \
                _LIST_INSERT_BETWEEN(name, _head, _where, _next, _item);\
        } while(false)

/* Insert an item before another one */
#define LIST_INSERT_BEFORE(name,head,where,item)                        \
        do {                                                            \
                typeof(*(head)) **_head = &(head), *_where = (where),   \
                         *_prev, *_item = (item);                       \
                _where = _where ? _where : LIST_FIRST(name, *_head);    \
                _prev = _where ? LIST_PREV(name, _where) : NULL;        \
                _LIST_INSERT_BETWEEN(name, _head, _prev, _where, _item);\
        } while(false)

#define LIST_MERGE_LIST(name, head_a, head_b)                           \
        do {                                                            \
                typeof(*(head_a)) **_a = &(head_a), **_b = &(head_b),   \
                        *_tail_a, *_head_b;                             \
                if (!*_a)                                               \
                        *_a = *_b;                                      \
                else if (*_b) {                                         \
                        _tail_a = LIST_LAST(name, *_a);                 \
                        _head_b = LIST_FIRST(name, *_b);                \
                                                                        \
                        _tail_a->name##_next = _head_b;               \
                        _head_b->name##_prev = _tail_a;               \
                        (*_a)->name##_other_end = _head_b->name##_other_end; \
                        (*_a)->name##_other_end->name##_other_end = *_a; \
                        _tail_a->name##_other_end = NULL;             \
                        _head_b->name##_other_end = NULL;             \
                }                                                       \
                                                                        \
                *_b = NULL;                                             \
        } while (false)

/* Remove an item from the list */
#define LIST_REMOVE(name,head,item)                                     \
        do {                                                            \
                typeof(*(head)) **_head = &(head), *_item = (item);     \
                typeof(*(head)) *_new_end = NULL;                       \
                assert(_item);                                          \
                if (!LIST_IN_LIST(name,_item))                          \
                        break;                                          \
                if (_item->name##_next)                                 \
                        _item->name##_next->name##_prev = _item->name##_prev; \
                else                                                    \
                        _new_end = _item->name##_prev;                  \
                if (_item->name##_prev)                                 \
                        _item->name##_prev->name##_next = _item->name##_next; \
                else if (_item->name##_other_end) {                     \
                        assert(*_head == _item);                        \
                        *_head = _item->name##_next;                    \
                        _new_end = _item->name##_next;                  \
                }                                                       \
                if (_new_end) {                                         \
                        assert(_item->name##_other_end);                \
                        _new_end->name##_other_end = _item->name##_other_end; \
                        _item->name##_other_end->name##_other_end = _new_end; \
                }                                                       \
                _item->name##_next = _item->name##_prev = _item->name##_other_end = NULL; \
        } while(false)

#define _LIST_STEAL(name, head, which)                                  \
        ({                                                              \
                typeof(*(head)) **_h = &(head), *_r = NULL;             \
                if ((_r = CONCATENATE(LIST_,which)(name, *_h)))         \
                        LIST_REMOVE(name, *_h, _r);                     \
                _r;                                                     \
        })

#define LIST_STEAL_FIRST(name,head)                                     \
        _LIST_STEAL(name, head, FIRST)

#define LIST_STEAL_LAST(name,head)                                      \
        _LIST_STEAL(name, head, LAST)

/* wether or not item is part of a list */
#define LIST_IN_LIST(name,item)                                         \
        ((item)->name##_next || (item)->name##_prev || (item)->name##_other_end)

#define LIST_JUST_US(name,item)                                         \
        (!(item)->name##_prev && !(item)->name##_next &&                \
                (item)->name##_other_end == (item))

#define LIST_FOREACH(name,i,head)                                       \
        for ((i) = (head); (i); (i) = (i)->name##_next)

#define LIST_FOREACH_REVERSE(name,i,head)                               \
        for ((i) = (head)?(head)->name##_other_end:NULL; (i); (i) = (i)->name##_prev)

#define LIST_FOREACH_SAFE(name,i,n,head)                                \
        for ((i) = (head); (i) && (((n) = (i)->name##_next), 1); (i) = (n))

#define LIST_FOREACH_BEFORE(name,i,p)                                   \
        for ((i) = (p)->name##_prev; (i); (i) = (i)->name##_prev)

#define LIST_FOREACH_AFTER(name,i,p)                                    \
        for ((i) = (p)->name##_next; (i); (i) = (i)->name##_next)

/* Loop starting from p->next until p->prev.
   p can be adjusted meanwhile. */
#define LIST_LOOP_BUT_ONE(name,i,p)                                             \
        for ((i) = (p)->name##_next ? (p)->name##_next : (p)->name##_other_end; \
             (i) != (p);                                                        \
             (i) = (i)->name##_next ? (i)->name##_next : (i)->name##_other_end)

/* get a list from a hashmap, add the item, and update the hashmap */
#define HASHMAP_LIST_PREPEND(name, hashmap, key_lambda, item)                   \
        _HASHMAP_LIST_ADD(PREPEND, name, hashmap, key_lambda, item)
#define HASHMAP_LIST_APPEND(name, hashmap, key_lambda, item)                    \
        _HASHMAP_LIST_ADD(APPEND, name, hashmap, key_lambda, item)
#define _HASHMAP_LIST_ADD(how, name, hashmap, key_lambda, item)                 \
        ({                                                                      \
                typeof(hashmap) _map = (hashmap);                               \
                typeof(*(item)) *_it = (item), *_h;                             \
                int _r = 0;                                                     \
                _h = hashmap_get(_map, LAMBDA(_it, key_lambda));                \
                CONCATENATE(LIST_,how)(name, _h, _it);                          \
                _r = hashmap_replace(_map, LAMBDA(_it, key_lambda), _h);        \
                if (_r < 0)                                                     \
                        LIST_REMOVE(name, _h, _it);                             \
                _r;                                                             \
        })

#define HASHMAP_LIST_REMOVE(name, hashmap, key_lambda, item)                    \
        ({                                                                      \
                typeof(hashmap) _map = (hashmap);                               \
                typeof(*(item)) *_it = (item), *_h;                             \
                int _r = 0;                                                     \
                _h = hashmap_get(_map, LAMBDA(_it, key_lambda));                \
                LIST_REMOVE(name, _h, _it);                                     \
                if (LIST_EMPTY(_h))                                             \
                        hashmap_remove(_map, LAMBDA(_it, key_lambda));          \
                else                                                            \
                        _r = hashmap_replace(_map, LAMBDA(_h, key_lambda), _h); \
                _r;                                                             \
        })
