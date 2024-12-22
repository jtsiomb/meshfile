/* dynarr - dynamic resizable C array data structure
 * author: John Tsiombikas <nuclear@member.fsf.org>
 * license: public domain
 */
#ifndef DYNARR_H_
#define DYNARR_H_

#include <stdlib.h>

/* usage example:
 * -------------
 * int *arr = mf_dynarr_alloc(0, sizeof *arr);
 *
 * int x = 10;
 * arr = mf_dynarr_push(arr, &x);
 * x = 5;
 * arr = mf_dynarr_push(arr, &x);
 * x = 42;
 * arr = mf_dynarr_push(arr, &x);
 *
 * for(i=0; i<mf_dynarr_size(arr); i++) {
 *     printf("%d\n", arr[i]);
 *  }
 *  mf_dynarr_free(arr);
 */

void *mf_dynarr_alloc(int elem, int szelem);
void mf_dynarr_free(void *da);
void *mf_dynarr_resize(void *da, int elem);

/* mf_dynarr_empty returns non-zero if the array is empty
 * Complexity: O(1) */
int mf_dynarr_empty(void *da);
/* mf_dynarr_size returns the number of elements in the array
 * Complexity: O(1) */
int mf_dynarr_size(void *da);

void *mf_dynarr_clear(void *da);

/* stack semantics */
void *mf_dynarr_push(void *da, void *item);
void *mf_dynarr_pop(void *da);

/* Finalize the array. No more resizing is possible after this call.
 * Use free() instead of mf_dynarr_free() to deallocate a finalized array.
 * Returns pointer to the finalized array.
 * mf_dynarr_finalize can't fail.
 * Complexity: O(n)
 */
void *mf_dynarr_finalize(void *da);

/* helper macros */
#define DYNARR_RESIZE(da, n) \
	do { (da) = mf_dynarr_resize((da), (n)); } while(0)
#define DYNARR_CLEAR(da) \
	do { (da) = mf_dynarr_clear(da); } while(0)
#define DYNARR_PUSH(da, item) \
	do { (da) = mf_dynarr_push((da), (item)); } while(0)
#define DYNARR_POP(da) \
	do { (da) = mf_dynarr_pop(da); } while(0)

/* utility macros to push characters to a string. assumes and maintains
 * the invariant that the last element is always a zero
 */
#define DYNARR_STRPUSH(da, c) \
	do { \
		char cnull = 0, ch = (char)(c); \
		(da) = mf_dynarr_pop(da); \
		(da) = mf_dynarr_push((da), &ch); \
		(da) = mf_dynarr_push((da), &cnull); \
	} while(0)

#define DYNARR_STRPOP(da) \
	do { \
		char cnull = 0; \
		(da) = mf_dynarr_pop(da); \
		(da) = mf_dynarr_pop(da); \
		(da) = mf_dynarr_push((da), &cnull); \
	} while(0)

/* never-fail versions of dynarr calls */
void *mf_dynarr_alloc_ordie(int nelem, int sz);
#define mf_dynarr_resize_ordie(da, n) \
	do { if(!((da) = mf_dynarr_resize((da), (n)))) abort(); } while(0)
#define mf_dynarr_clear_ordie(da) \
	do { if(!((da) = mf_dynarr_clear(da))) abort(); } while(0)
#define mf_dynarr_push_ordie(da, item) \
	do { if(!((da) = mf_dynarr_push((da), (item)))) abort(); } while(0)
#define mf_dynarr_pop_ordie(da) \
	do { if(!((da) = mf_dynarr_pop(da))) abort(); } while(0)

#endif	/* DYNARR_H_ */
