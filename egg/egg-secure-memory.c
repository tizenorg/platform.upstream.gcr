/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* egg-secure-memory.h - library for allocating memory that is non-pageable

   Copyright (C) 2007 Stefan Walter

   The Gnome Keyring Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Keyring Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Stef Walter <stef@memberwebs.com>
*/

/*
 * IMPORTANT: This is pure vanila standard C, no glib. We need this
 * because certain consumers of this protocol need to be built
 * without linking in any special libraries. ie: the PKCS#11 module.
 */

#include "config.h"

#include "egg-secure-memory.h"

#include <sys/types.h>
#include <sys/mman.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

/*
 * Use this to force all memory through malloc
 * for use with valgrind and the like
 */
#define FORCE_FALLBACK_MEMORY 0

#define DEBUG_SECURE_MEMORY 0

#if DEBUG_SECURE_MEMORY
#define DEBUG_ALLOC(msg, n) 	fprintf(stderr, "%s %lu bytes\n", msg, n);
#else
#define DEBUG_ALLOC(msg, n)
#endif

#define DEFAULT_BLOCK_SIZE 16384

/* Use our own assert to guarantee no glib allocations */
#ifndef ASSERT
#ifdef G_DISABLE_ASSERT
#define ASSERT(x)
#else
#define ASSERT(x) assert(x)
#endif
#endif

#define DO_LOCK() \
	egg_memory_lock ();

#define DO_UNLOCK() \
	egg_memory_unlock ();

static int lock_warning = 1;


/*
 * We allocate all memory in units of sizeof(void*). This
 * is our definition of 'word'.
 */
typedef void* word_t;

/* The amount of extra words we can allocate */
#define WASTE   4

/*
 * Track allocated memory or a free block. This structure is not stored
 * in the secure memory area. It is allocated from a pool of other
 * memory. See meta_pool_xxx ().
 */
typedef struct _Cell {
	word_t *words;          /* Pointer to secure memory */
	size_t n_words;         /* Amount of secure memory in words */
	size_t allocated;       /* Amount actually requested by app, in bytes, 0 if unused */
	struct _Cell *next;     /* Next in unused memory ring, or NULL if used */
	struct _Cell *prev;     /* Previous in unused memory ring, or NULL if used */
} Cell;

/*
 * A block of secure memory. This structure is the header in that block.
 */
typedef struct _Block {
	word_t *words;          /* Actual memory hangs off here */
	size_t n_words;         /* Number of words in block */
	size_t used;            /* Number of used allocations */
	struct _Cell* unused;   /* Ring of unused allocations */
	struct _Block *next;    /* Next block in list */
} Block;

/* -----------------------------------------------------------------------------
 * UNUSED STACK
 */

static inline void
unused_push (void **stack, void *ptr)
{
	ASSERT (ptr);
	ASSERT (stack);
	*((void**)ptr) = *stack;
	*stack = ptr;
}

static inline void*
unused_pop (void **stack)
{
	void *ptr;
	ASSERT (stack);
	ptr = *stack;
	*stack = *(void**)ptr;
	return ptr;

}

static inline void*
unused_peek (void **stack)
{
	ASSERT (stack);
	return *stack;
}

/* -----------------------------------------------------------------------------
 * POOL META DATA ALLOCATION
 *
 * A pool for memory meta data. We allocate fixed size blocks. There are actually
 * two different structures stored in this pool: Cell and Block. Cell is allocated
 * way more often, and is bigger so we just allocate that size for both.
 */

/* Pool allocates this data type */
typedef struct _Item {
	union {
		Cell cell;
		Block block;
	};
} Item;

typedef struct _Pool {
	struct _Pool *next;    /* Next pool in list */
	size_t length;         /* Length in bytes of the pool */
	size_t used;           /* Number of cells used in pool */
	void *unused;          /* Unused stack of unused stuff */
	size_t n_items;        /* Total number of items in pool */
	Item items[1];         /* Actual items hang off here */
} Pool;

static Pool *all_pools = NULL;

static void*
pool_alloc (void)
{
	Pool *pool;
	void *pages;
	size_t len, i;

	/* A pool with an available item */
	for (pool = all_pools; pool; pool = pool->next) {
		if (unused_peek (&pool->unused))
			break;
	}

	/* Create a new pool */
	if (pool == NULL) {
		len = getpagesize () * 2;
		pages = mmap (0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (pages == MAP_FAILED)
			return NULL;

		/* Fill in the block header, and inlude in block list */
		pool = pages;
		pool->next = all_pools;
		all_pools = pool;
		pool->length = len;
		pool->used = 0;
		pool->unused = NULL;

		/* Fill block with unused items */
		pool->n_items = (len - sizeof (Pool)) / sizeof (Item);
		for (i = 0; i < pool->n_items; ++i)
			unused_push (&pool->unused, pool->items + i);
	}

	++pool->used;
	ASSERT (unused_peek (&pool->unused));
	return memset (unused_pop (&pool->unused), 0, sizeof (Item));
}

static void
pool_free (void* item)
{
	Pool *pool, **at;
	char *ptr, *beg, *end;

	ptr = item;

	/* Find which block this one belongs to */
	for (at = &all_pools, pool = *at; pool; at = &pool->next, pool = *at) {
		beg = (char*)pool->items;
		end = (char*)pool + pool->length - sizeof (Item);
		if (ptr >= beg && ptr <= end) {
			ASSERT ((ptr - beg) % sizeof (Item) == 0);
			break;
		}
	}

	/* Otherwise invalid meta */
	ASSERT (pool && *at);
	ASSERT (pool->used > 0);

	/* No more meta cells used in this block, remove from list, destroy */
	if (pool->used == 1) {
		*at = pool->next;
		munmap (pool, pool->length);
		return;
	}

	--pool->used;
	memset (item, 0xCD, sizeof (Item));
	unused_push (&pool->unused, item);
}

static int
pool_valid (void* item)
{
	Pool *pool;
	char *ptr, *beg, *end;

	ptr = item;

	/* Find which block this one belongs to */
	for (pool = all_pools; pool; pool = pool->next) {
		beg = (char*)pool->items;
		end = (char*)pool + pool->length - sizeof (Item);
		if (ptr >= beg && ptr <= end)
			return (pool->used && (ptr - beg) % sizeof (Item) == 0);
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * SEC ALLOCATION
 *
 * Each memory cell begins and ends with a pointer to its metadata.
 *
 */

static inline size_t
sec_size_to_words (size_t length)
{
	return (length % sizeof (void*) ? 1 : 0) + (length / sizeof (void*));
}

static inline void
sec_write_guards (Cell *cell)
{
	((void**)cell->words)[0] = (void*)cell;
	((void**)cell->words)[cell->n_words - 1] = (void*)cell;
}

static inline void
sec_check_guards (Cell *cell)
{
	ASSERT(((void**)cell->words)[0] == (void*)cell);
	ASSERT(((void**)cell->words)[cell->n_words - 1] == (void*)cell);
}

static void
sec_insert_cell_ring (Cell **ring, Cell *cell)
{
	ASSERT (ring);
	ASSERT (cell);
	ASSERT (cell != *ring);
	ASSERT (cell->next == NULL);
	ASSERT (cell->prev == NULL);

	/* Insert back into the mix of available memory */
	if (*ring) {
		cell->next = (*ring)->next;
		cell->prev = (*ring)->prev;
		cell->next->prev = cell;
		cell->prev->next = cell;
	} else {
		cell->next = cell;
		cell->prev = cell;
	}

	*ring = cell;
}

static void
sec_remove_cell_ring (Cell **ring, Cell *cell)
{
	ASSERT (ring);
	ASSERT (*ring);
	ASSERT (cell->next);
	ASSERT (cell->prev);

	if (cell == *ring) {
		/* The last meta? */
		if (cell->next == cell) {
			ASSERT (cell->prev == cell);
			*ring = NULL;

		/* Just pointing to this meta */
		} else {
			ASSERT (cell->prev != cell);
			*ring = cell->next;
		}
	}

	cell->next->prev = cell->prev;
	cell->prev->next = cell->next;
	cell->next = cell->prev = NULL;

	ASSERT (*ring != cell);
}

static inline void*
sec_cell_to_memory (Cell *cell)
{
	return cell->words + 1;
}

static inline int
sec_is_valid_word (Block *block, word_t *word)
{
	return (word >= block->words && word < block->words + block->n_words);
}

static inline void*
sec_clear_memory (void *memory, size_t from, size_t to)
{
	ASSERT (from <= to);
	memset ((char*)memory + from, 0, to - from);
	return memory;
}

static Cell*
sec_neighbor_before (Block *block, Cell *cell)
{
	word_t *word;

	ASSERT (cell);
	ASSERT (block);

	word = cell->words - 1;
	if (!sec_is_valid_word (block, word))
		return NULL;

	cell = *word;
	sec_check_guards (cell);
	return cell;
}

static Cell*
sec_neighbor_after (Block *block, Cell *cell)
{
	word_t *word;

	ASSERT (cell);
	ASSERT (block);

	word = cell->words + cell->n_words;
	if (!sec_is_valid_word (block, word))
		return NULL;

	cell = *word;
	sec_check_guards (cell);
	return cell;
}

static void*
sec_alloc (Block *block, size_t length)
{
	Cell *cell, *other;
	size_t n_words;

	ASSERT (block);
	ASSERT (length);

	if (!block->unused)
		return NULL;

	/*
	 * Each memory allocation is aligned to a pointer size, and
	 * then, sandwidched between two pointers to its meta data.
	 * These pointers also act as guards.
	 *
	 * We allocate memory in units of sizeof (void*)
	 */

	n_words = sec_size_to_words (length) + 2;

	/* Look for a cell of at least our required size */
	cell = block->unused;
	while (cell->n_words < n_words) {
		cell = cell->next;
		if (cell == block->unused) {
			cell = NULL;
			break;
		}
	}

	if (!cell)
		return NULL;

	ASSERT (cell->allocated == 0);
	ASSERT (cell->prev);
	ASSERT (cell->words);
	sec_check_guards (cell);

	/* Steal from the cell if it's too long */
	if (cell->n_words > n_words + WASTE) {
		other = pool_alloc ();
		if (!other)
			return NULL;
		other->n_words = n_words;
		other->words = cell->words;
		cell->n_words -= n_words;
		cell->words += n_words;

		sec_write_guards (other);
		sec_write_guards (cell);

		cell = other;
	}

	if (cell->next)
		sec_remove_cell_ring (&block->unused, cell);

	++block->used;
	cell->allocated = length;
	return memset (sec_cell_to_memory (cell), 0, cell->allocated);
}

static void*
sec_free (Block *block, void *memory)
{
	Cell *cell, *other;
	word_t *word;

	ASSERT (block);
	ASSERT (memory);

	/* Lookup the meta for this memory block (using guard pointer) */
	word = memory;
	ASSERT (sec_is_valid_word (block, word - 1));
	ASSERT (pool_valid (*(word - 1)));
	cell = *(word - 1);

	sec_check_guards (cell);
	sec_clear_memory (memory, 0, cell->allocated);

	sec_check_guards (cell);
	ASSERT (cell->next == NULL);
	ASSERT (cell->prev == NULL);
	ASSERT (cell->allocated > 0);

        /* Find previous unallocated neighbor, and merge if possible */
        other = sec_neighbor_before (block, cell);
        if (other && other->allocated == 0) {
        	ASSERT (other->next && other->prev);
        	other->n_words += cell->n_words;
        	sec_write_guards (other);
        	pool_free (cell);
        	cell = other;
        }

        /* Find next unallocated neighbor, and merge if possible */
        other = sec_neighbor_after (block, cell);
        if (other && other->allocated == 0) {
        	ASSERT (other->next && other->prev);
        	other->n_words += cell->n_words;
        	other->words = cell->words;
        	if (cell->next)
        		sec_remove_cell_ring (&block->unused, cell);
        	sec_write_guards (other);
        	pool_free (cell);
        	cell = other;
        }

        /* Add to the unused list if not already there */
        if (!cell->next)
        	sec_insert_cell_ring (&block->unused, cell);

        cell->allocated = 0;
        --block->used;
        return NULL;
}

static void*
sec_realloc (Block *block, void *memory, size_t length)
{
	Cell *cell, *other;
	word_t *word;
	size_t n_words;
	size_t valid;
	void *alloc;

	/* Standard realloc behavior, should have been handled elsewhere */
	ASSERT (memory != NULL);
	ASSERT (length > 0);

	/* Dig out where the meta should be */
	word = memory;
	ASSERT (sec_is_valid_word (block, word - 1));
	ASSERT (pool_valid (*(word - 1)));
	cell = *(word - 1);

	/* Validate that it's actually for real */
	sec_check_guards (cell);
	ASSERT (cell->allocated > 0);
	ASSERT (cell->next == NULL);
	ASSERT (cell->prev == NULL);

	/* The amount of valid data */
	valid = cell->allocated;

	/* How many words we actually want */
	n_words = sec_size_to_words (length) + 2;

	/* Less memory is required */
	if (n_words <= cell->n_words) {

		/* TODO: No shrinking behavior yet */
		cell->allocated = length;
		return sec_clear_memory (sec_cell_to_memory (cell), valid, length);
	}

	/* Need braaaaaiiiiiinsss... */
	while (cell->n_words < n_words) {

		/* See if we have a neighbor who can give us some memory */
		other = sec_neighbor_after (block, cell);
		if (!other || other->allocated != 0)
			break;

		/* Eat the whole neighbor if not too big */
		if (n_words - cell->n_words >= other->n_words + WASTE) {
			cell->n_words += other->n_words;
			sec_write_guards (cell);
			sec_remove_cell_ring (&block->unused, other);
			pool_free (other);

		/* Steal from the neighbor */
		} else {
			other->words += n_words - cell->n_words;
			other->n_words -= n_words - cell->n_words;
			sec_write_guards (other);
			cell->n_words = n_words;
			sec_write_guards (cell);
		}
	}

	if (cell->n_words >= n_words) {
		cell->allocated = length;
		return sec_clear_memory (sec_cell_to_memory (cell), valid, length);
	}

	/* That didn't work, try alloc/free */
	alloc = sec_alloc (block, length);
	if (alloc) {
		memcpy (alloc, memory, valid);
		sec_free (block, memory);
	}

	return alloc;
}


static size_t
sec_allocated (Block *block, void *memory)
{
	Cell *cell;
	word_t *word;

	ASSERT (block);
	ASSERT (memory);

	/* Lookup the meta for this memory block (using guard pointer) */
	word = memory;
	ASSERT (sec_is_valid_word (block, word - 1));
	ASSERT (pool_valid (*(word - 1)));
	cell = *(word - 1);

	sec_check_guards (cell);
	ASSERT (cell->next == NULL);
	ASSERT (cell->prev == NULL);
	ASSERT (cell->allocated > 0);

	return cell->allocated;
}

/* -----------------------------------------------------------------------------
 * LOCKED MEMORY
 */

static void*
sec_acquire_pages (unsigned long *sz)
{
	void *pages;
	unsigned long pgsize;

	ASSERT (sz);
	ASSERT (*sz);

	/* Make sure sz is a multiple of the page size */
	pgsize = getpagesize ();
	*sz = (*sz + pgsize -1) & ~(pgsize - 1);

#if defined(HAVE_MLOCK)
	pages = mmap (0, *sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (pages == MAP_FAILED) {
		if (lock_warning)
			fprintf (stderr, "couldn't map %lu bytes of private memory: %s\n",
			         *sz, strerror (errno));
		lock_warning = 0;
		return NULL;
	}

	if (mlock (pages, *sz) < 0) {
		if (lock_warning && errno != EPERM) {
			fprintf (stderr, "couldn't lock %lu bytes of private memory: %s\n",
			         *sz, strerror (errno));
			lock_warning = 0;
		}
		munmap (pages, *sz);
		return NULL;
	}

	DEBUG_ALLOC ("gkr-secure-memory: new block ", *sz);

	lock_warning = 1;
	return pages;

#else
	if (lock_warning)
		fprintf (stderr, "your system does not support private memory");
	lock_warning = 0;
	return NULL;
#endif

}

static void
sec_release_pages (void *pages, unsigned long sz)
{
	ASSERT (pages);
	ASSERT (sz % getpagesize () == 0);

#if defined(HAVE_MLOCK)
	if (munlock (pages, sz) < 0)
		fprintf (stderr, "couldn't unlock private memory: %s\n", strerror (errno));

	if (munmap (pages, sz) < 0)
		fprintf (stderr, "couldn't unmap private anonymous memory: %s\n", strerror (errno));

	DEBUG_ALLOC ("gkr-secure-memory: freed block ", sz);

#else
	ASSERT (FALSE);
#endif
}

/* -----------------------------------------------------------------------------
 * MANAGE DIFFERENT BLOCKS
 */

static Block *all_blocks = NULL;

static Block*
sec_block_create (unsigned long size)
{
	Block *block;
	Cell *cell;

#if FORCE_FALLBACK_MEMORY
	/* We can force all all memory to be malloced */
	return NULL;
#endif

	block = pool_alloc ();
	if (!block)
		return NULL;

	cell = pool_alloc ();
	if (!cell) {
		pool_free (block);
		return NULL;
	}

	/* The size above is a minimum, we're free to go bigger */
	if (size < DEFAULT_BLOCK_SIZE)
		size = DEFAULT_BLOCK_SIZE;

	block->n_words = size / sizeof (word_t);
	block->words = sec_acquire_pages (&size);
	if (!block->words) {
		pool_free (block);
		pool_free (cell);
		return NULL;
	}

	/* The first cell to allocate from */
	cell->words = block->words;
	cell->n_words = block->n_words;
	cell->allocated = 0;
	sec_write_guards (cell);
	sec_insert_cell_ring (&block->unused, cell);

	block->next = all_blocks;
	all_blocks = block;

	return block;
}

static void
sec_block_destroy (Block *block)
{
	Block *bl, **at;
	Cell *cell;

	ASSERT (block);
	ASSERT (block->words);
	ASSERT (block->used == 0);

	/* Remove from the list */
	for (at = &all_blocks, bl = *at; bl; at = &bl->next, bl = *at) {
		if (bl == block) {
			*at = block->next;
			break;
		}
	}

	/* Must have been found */
	ASSERT (bl == block);

	/* Release all the meta data cells */
	while (block->unused) {
		cell = block->unused;
		sec_remove_cell_ring (&block->unused, cell);
		pool_free (cell);
	}

	/* Release all pages of secure memory */
	sec_release_pages (block->words, block->n_words * sizeof (word_t));

	pool_free (block);
}

/* ------------------------------------------------------------------------
 * PUBLIC FUNCTIONALITY
 */

void*
egg_secure_alloc (size_t length)
{
	return egg_secure_alloc_full (length, GKR_SECURE_USE_FALLBACK);
}

void*
egg_secure_alloc_full (size_t length, int flags)
{
	Block *block;
	void *memory = NULL;

	if (length > 0xFFFFFFFF / 2) {
		fprintf (stderr, "tried to allocate an insane amount of memory: %lu\n", length);
		return NULL;
	}

	DO_LOCK ();

		for (block = all_blocks; block; block = block->next) {
			memory = sec_alloc (block, length);
			if (memory)
				break;
		}

		/* None of the current blocks have space, allocate new */
		if (!memory) {
			block = sec_block_create (length);
			if (block)
				memory = sec_alloc (block, length);
		}

	DO_UNLOCK ();

	if (!memory && (flags & GKR_SECURE_USE_FALLBACK)) {
		memory = egg_memory_fallback (NULL, length);
		if (memory) /* Our returned memory is always zeroed */
			memset (memory, 0, length);
	}

	if (!memory)
		errno = ENOMEM;

	return memory;
}

void*
egg_secure_realloc (void *memory, size_t length)
{
	return egg_secure_realloc_full (memory, length, GKR_SECURE_USE_FALLBACK);
}

void*
egg_secure_realloc_full (void *memory, unsigned long length, int flags)
{
	Block *block = NULL;
	size_t previous = 0;
	int donew = 0;
	void *alloc = NULL;

	if (length > 0xFFFFFFFF / 2) {
		fprintf (stderr, "tried to allocate an insane amount of memory: %lu\n", length);
		ASSERT (0 && "tried to allocate an insane amount of memory");
		return NULL;
	}

	if (memory == NULL)
		return egg_secure_alloc_full (length, flags);
	if (!length) {
		egg_secure_free_full (memory, flags);
		return NULL;
	}

	DO_LOCK ();

		/* Find out where it belongs to */
		for (block = all_blocks; block; block = block->next) {
			if (sec_is_valid_word (block, memory)) {
				previous = sec_allocated (block, memory);
				alloc = sec_realloc (block, memory, length);
				break;
			}
		}

		/* If it didn't work we may need to allocate a new block */
		if (block && !alloc)
			donew = 1;

		if (block && block->used == 0)
			sec_block_destroy (block);

	DO_UNLOCK ();

	if (!block) {
		if ((flags & GKR_SECURE_USE_FALLBACK)) {
			/*
			 * In this case we can't zero the returned memory,
			 * because we don't know what the block size was.
			 */
			return egg_memory_fallback (memory, length);
		} else {
			fprintf (stderr, "memory does not belong to gnome-keyring: 0x%08lx\n", (unsigned long)memory);
			ASSERT (0 && "memory does does not belong to gnome-keyring");
			return NULL;
		}
	}

	if (donew) {
		alloc = egg_secure_alloc_full (length, flags);
		if (alloc) {
			memcpy (alloc, memory, previous);
			egg_secure_free_full (memory, flags);
		}
	}

	if (!alloc)
		errno = ENOMEM;

	return alloc;
}

void
egg_secure_free (void *memory)
{
	egg_secure_free_full (memory, GKR_SECURE_USE_FALLBACK);
}

void
egg_secure_free_full (void *memory, int flags)
{
	Block *block = NULL;

	if (memory == NULL)
		return;

	DO_LOCK ();

		/* Find out where it belongs to */
		for (block = all_blocks; block; block = block->next) {
			if (sec_is_valid_word (block, memory)) {
				sec_free (block, memory);
				break;
			}
		}

		if (block && block->used == 0)
			sec_block_destroy (block);

	DO_UNLOCK ();

	if (!block) {
		if ((flags & GKR_SECURE_USE_FALLBACK)) {
			egg_memory_fallback (memory, 0);
		} else {
			fprintf (stderr, "memory does not belong to gnome-keyring: 0x%08lx\n", (unsigned long)memory);
			ASSERT (0 && "memory does does not belong to gnome-keyring");
		}
	}
}

int
egg_secure_check (const void *memory)
{
	Block *block = NULL;

	DO_LOCK ();

		/* Find out where it belongs to */
		for (block = all_blocks; block; block = block->next) {
			if (sec_is_valid_word (block, (word_t*)memory))
				break;
		}

	DO_UNLOCK ();

	return block == NULL ? 0 : 1;
}

void
egg_secure_dump_blocks (void)
{
	Block *block = NULL;

	DO_LOCK ();

		/* Find out where it belongs to */
		for (block = all_blocks; block; block = block->next) {
			fprintf (stderr, "----------------------------------------------------\n");
			fprintf (stderr, "  BLOCK at: 0x%08lx  len: %lu\n", (unsigned long)block, block->n_words * sizeof (word_t));
			fprintf (stderr, "\n");
		}

	DO_UNLOCK ();
}

char*
egg_secure_strdup (const char *str)
{
	unsigned long len;
	char *res;

	if (!str)
		return NULL;

	len = strlen (str) + 1;
	res = (char*)egg_secure_alloc (len);
	strcpy (res, str);
	return res;
}

void
egg_secure_strclear (char *str)
{
	volatile char *vp;
	size_t len;

	if (!str)
		return;

        vp = (volatile char*)str;
       	len = strlen (str);
        while (len) {
        	*vp = 0xAA;
        	vp++;
        	len--;
        }
}

void
egg_secure_strfree (char *str)
{
	/*
	 * If we're using unpageable 'secure' memory, then the free call
	 * should zero out the memory, but because on certain platforms
	 * we may be using normal memory, zero it out here just in case.
	 */

	egg_secure_strclear (str);
	egg_secure_free_full (str, GKR_SECURE_USE_FALLBACK);
}