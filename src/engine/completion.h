/* vifm
 * Copyright (C) 2011 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef VIFM__ENGINE__COMPLETION_H__
#define VIFM__ENGINE__COMPLETION_H__

/* Single completion item. */
typedef struct
{
	char *text;  /* Item text. */
	char *descr; /* Description of the item. */
}
vle_compl_t;

/* Match addition hook function signature.  Must return newly allocated
 * string. */
typedef char * (*vle_compl_add_path_hook_f)(const char match[]);

/* Type of a custom qsort()-like completion sorter.  Inputs are already
 * normalized. */
typedef int (*vle_compl_sorter_f)(const char a[], const char b[]);

/* Adds raw match as completion match.  Returns zero on success, otherwise
 * non-zero is returned. */
int vle_compl_add_match(const char match[], const char descr[]);

/* Puts raw match as completion match, takes ownership of the match string.
 * Returns zero on success, otherwise non-zero is returned (including when match
 * is NULL). */
int vle_compl_put_match(char match[], const char descr[]);

/* Adds path as completion match.  Path is preprocessed with path add hook.
 * Returns zero on success, otherwise non-zero is returned. */
int vle_compl_add_path_match(const char path[]);

/* Puts path as completion match, takes ownership of the match string.  Path is
 * preprocessed with path add hook.  Returns zero on success, otherwise non-zero
 * is returned. */
int vle_compl_put_path_match(char path[]);

/* Adds original input to the completion, should be called after all matches are
 * registered with vle_compl_add_match().  Returns zero on success, otherwise
 * non-zero is returned. */
int vle_compl_add_last_match(const char origin[]);

/* Adds original path path input to the completion, should be called after all
 * matches are registered with vle_compl_add_path_match().  Returns zero on
 * success, otherwise non-zero is returned. */
int vle_compl_add_last_path_match(const char origin[]);

void vle_compl_finish_group(void);

/* Squashes all existing completion groups into one.  Performs resorting and
 * deduplication of resulting single group. */
void vle_compl_unite_groups(void);

void vle_compl_reset(void);

/* Returns copy of the string or NULL. */
char * vle_compl_next(void);

int vle_compl_get_count(void);

/* Sets direction from which the next completion item is selected.
 * vle_compl_reset() resets to forward direction which is the default.  The
 * direction can be changed at any moment. */
void vle_compl_set_reversed(int reversed);

/* Sets or resets (when the parameter is NULL) a custom completion sorter.  The
 * sorter is always reset by vle_compl_reset().  Must be called after
 * vle_compl_reset() before adding any matches or querying completions. */
void vle_compl_set_sorter(vle_compl_sorter_f sorter);

/* Retrieves list of completion items.  Returns the list of size
 * vle_compl_get_count().  The array is managed by the unit. */
const vle_compl_t * vle_compl_get_items(void);

int vle_compl_get_pos(void);

/* Go to the last item (probably to user input). */
void vle_compl_rewind(void);

/* Sets match addition hook.  NULL value resets hook. */
void vle_compl_set_add_path_hook(vle_compl_add_path_hook_f hook);

#endif /* VIFM__ENGINE__COMPLETION_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
