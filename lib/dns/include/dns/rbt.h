/*
 * Copyright (C) 1999  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef	DNS_RBT_H
#define	DNS_RBT_H 1

#include <isc/lang.h>
#include <isc/result.h>
#include <isc/mem.h>

#include <dns/types.h>
#include <dns/name.h>

ISC_LANG_BEGINDECLS

/*
 * This is the structure that is used for each node in the red/black
 * tree of trees.  NOTE WELL:  the implementation manages this as a variable
 * length structure, with the actual wire-format name and other data appended
 * appended to this structure.  Allocating a contiguous block of memory for
 * multiple dns_rbtnode structures will not work.
 */

typedef struct dns_rbt dns_rbt_t;

/* These should add up to 31 */

#define DNS_RBT_LOCKLENGTH			11
#define DNS_RBT_REFLENGTH			20

typedef struct dns_rbtnode {
	struct dns_rbtnode *left;
	struct dns_rbtnode *right;
	struct dns_rbtnode *down;
	/*
	 * The following bitfields add up to a total bitwidth of 32.
	 * The range of values necessary for each item is indicated,
	 * but in the case of "attributes" the field is wider to accomodate
	 * possible future expansion.  "offsetlen" could be one bit
	 * narrower by always adjusting its value by 1 to find the real
	 * offsetlen, but doing so does not gain anything (except perhaps
	 * another bit for "attributes", which doesn't yet need any more).
	 *
	 * In each case below the "range" indicated is what's _necessary_ for
	 * the bitfield to hold, not what it actually _can_ hold.
	 */
	unsigned int color:1;	      /* range is 0..1 */
	unsigned int find_callback:1; /* range is 0..1 */
	unsigned int attributes:5;    /* range is 0..2 */
	unsigned int namelen:8;	      /* range is 1..255 */
	unsigned int offsetlen:8;     /* range is 1..128 */
	unsigned int padbytes:9;      /* range is 0..380 */
	/*
	 * These values are used in the RBT DB implementation.  The appropriate
	 * node lock must be held before accessing them.
	 */
	void *data;
	unsigned int dirty:1;
	unsigned int locknum:DNS_RBT_LOCKLENGTH;
	unsigned int references:DNS_RBT_REFLENGTH;
} dns_rbtnode_t;

typedef dns_result_t (*dns_rbtfindcallback_t)(dns_rbtnode_t *node,
					      dns_name_t *name,
					      void *callback_arg);

/*****
 *****	Chain Info
 *****/

/*
 * A chain is used to keep track of the sequence of nodes to reach any given
 * node from the root of the tree.  Since no parent pointer is stored with
 * each node, it is the only way to know what is "up" from any particular
 * node, which is necessary information for iterating through the tree or
 * for basic internal tree maintenance issues (ie, the rotations that are
 * done to rebalance the tree when a node is added).  The obvious implication
 * of this is that for a chain to remain valid, the tree has to be locked
 * down against writes for the duration of the useful life of the chain,
 * because additions or removals can change the path from the root to the node
 * the chain has targetted.
 *
 * XXX add _current
 * The dns_rbtnodechain_ functions _first, _last, _prev and _next all take
 * dns_name_t parameters for the name and the origin.  'name' will end up
 * pointing to the name data and offsets that are stored at the node (and thus
 * it will be read-only), so it should be a regular dns_name_t that has been
 * initialized with dns_name_init.  'origin' will get the name of the origin
 * stored in it, so it needs to have its own buffer space and offsets, which
 * is most easily accomplished with a dns_fixedname_t.  It is _not_ necessary
 * to reinitialize either 'name' or 'origin' between calls to the chain
 * functions.
 */

/*
 * For use in allocating space for the chain of ancestor nodes.
 *
 * The maximum number of ancestors is theoretically not limited by the
 * data tree.  This initial value of 24 ancestors would be able to scan
 * the full height of a single level of 16,777,216 nodes, more than double
 * the current size of .com.
 */
#define DNS_RBT_ANCESTORBLOCK 24

/*
 * The number of level blocks to allocate at a time.  Currently the maximum
 * number of levels is allocated directly in the structure, but future revisions
 * of this code might treat levels like ancestors -- that is, have a static
 * initial block with dynamic growth.  Allocating space for 256 levels when
 * the tree is almost never that deep is wasteful, but it's not clear that
 * it matters, since the waste is only 2MB for 1000 concurrently active
 * chains on a system with 64-bit pointers.
 */
#define DNS_RBT_LEVELBLOCK 254

typedef struct dns_rbtnodechain {
	unsigned int		magic;
	isc_mem_t *		mctx;
	dns_rbtnode_t *		end;
	dns_rbtnode_t **	ancestors;
	/*
	 * ancestor_block avoids doing any memory allocation (a MP
	 * bottleneck) in 99%+ of the real-world cases.
	 */
	dns_rbtnode_t *		ancestor_block[DNS_RBT_ANCESTORBLOCK];
	unsigned int		ancestor_count;
	unsigned int		ancestor_maxitems;
	/*
	 * The maximum number of labels in a name is 128; bitstrings mean
	 * a conceptually very large number (which I have not bothered to
	 * compute) of logical levels because splitting can potentially occur
	 * at each bit.  However, DNSSEC restricts the number of "logical"
	 * labels in a name to 255, meaning only 254 pointers are needed
	 * in the worst case.
	 */
	dns_rbtnode_t *		levels[DNS_RBT_LEVELBLOCK];
	unsigned int		level_count;
	unsigned int		level_matches; /* XXX comment */
} dns_rbtnodechain_t;

/*****
 ***** Public interfaces.
 *****/

dns_result_t
dns_rbt_create(isc_mem_t *mctx, void (*deleter)(void *, void *),
	       void *deleter_arg, dns_rbt_t **rbtp);
/*
 * Initialize a red-black tree of trees.
 *
 * Notes:
 *	The deleter argument, if non-null, points to a function that is
 *	responsible for cleaning up any memory associated with the data
 *	pointer of a node when the node is deleted.  It is passed the
 *	deleted node's data pointer as its first argument and deleter_arg
 *	as its second argument.  
 *
 * Requires:
 * 	mctx is a pointer to a valid memory context.
 *	rbtp != NULL && *rbtp == NULL
 *	arg == NULL iff deleter == NULL
 *
 * Ensures:
 *	If result is DNS_R_SUCCESS:
 *		*rbtp points to a valid red-black tree manager
 *
 *	If result is failure:
 *		*rbtp does not point to a valid red-black tree manager.
 *
 * Returns:
 *	DNS_R_SUCCESS	Success
 *	DNS_R_NOMEMORY	Resource limit: Out of Memory
 */

dns_result_t
dns_rbt_addname(dns_rbt_t *rbt, dns_name_t *name, void *data);
/*
 * Add 'name' to the tree of trees, associated with 'data'.
 *
 * Notes:
 *	'data' is never required to be non-NULL, but specifying it
 *	when the name is added is faster than searching for 'name'
 *	again and then setting the data pointer.  The lack of a data pointer
 *	for a node also has other ramifications regarding whether
 *	dns_rbt_findname considers a node to exist, or dns_rbt_deletename
 *	joins nodes.
 *
 * Requires:
 *	rbt is a valid rbt manager.
 *	dns_name_isabsolute(name) == TRUE
 *
 * Ensures:
 *	'name' is not altered in any way.
 *
 *	Any external references to nodes in the tree are unaffected by
 *	node splits that are necessary to insert the new name.
 *
 *	If result is DNS_R_SUCCESS:
 *		'name' is findable in the red/black tree of trees in O(log N).
 *
 *		The data pointer of the node for 'name' is set to 'data'.
 *
 *	If result is DNS_R_EXISTS or DNS_R_NOSPACE:
 *		The tree of trees is unaltered.
 *
 *	If result is DNS_R_NOMEMORY:
 *		No guarantees.
 *
 * Returns:
 *	DNS_R_SUCCESS	Success
 *	DNS_R_EXISTS	The name already exists with associated data.
 *	DNS_R_NOSPACE 	The name had more logical labels than are allowed.
 *	DNS_R_NOMEMORY	Resource Limit: Out of Memory
 */

dns_result_t
dns_rbt_addnode(dns_rbt_t *rbt, dns_name_t *name, dns_rbtnode_t **nodep);

/*
 * Just like dns_rbt_addname, but returns the address of the node.
 *
 * Requires:
 *	rbt is a valid rbt structure.
 *	dns_name_isabsolute(name) == TRUE
 *	nodep != NULL && *nodep == NULL
 *
 * Ensures:
 *	'name' is not altered in any way.
 *
 *	Any external references to nodes in the tree are unaffected by
 *	node splits that are necessary to insert the new name.
 *
 *	If result is DNS_R_SUCCESS:
 *		'name' is findable in the red/black tree of trees in O(log N).
 *
 *		*nodep is the node that was added for 'name'.
 *
 *	If result is DNS_R_EXISTS:
 *		The tree of trees is unaltered.
 *
 *		*nodep is the existing node for 'name'.
 *
 *	If result is DNS_R_NOMEMORY:
 *		No guarantees.
 *
 * Returns:
 *	DNS_R_SUCCESS	Success
 *	DNS_R_EXISTS	The name already exists, possibly without data.
 *	DNS_R_NOMEMORY	Resource Limit: Out of Memory
 */

dns_result_t
dns_rbt_findname(dns_rbt_t *rbt, dns_name_t *name,
		 dns_name_t *foundname, void **data);
/*
 * Get the data pointer associated with 'name'.
 *
 * Notes:
 *	A node that has no data is considered not to exist for this function.
 *
 * Requires:
 *	rbt is a valid rbt manager.
 *	dns_name_isabsolute(name) == TRUE
 *	data != NULL && *data == NULL
 *
 * Ensures:
 *	'name' and the tree are not altered in any way.
 *
 *	If result is DNS_R_SUCCESS:
 *		*data is the data associated with 'name'.
 *
 *	If result is DNS_R_PARTIALMATCH:
 *		*data is the data associated with the deepest superdomain
 * 		of 'name' which has data.
 *
 *	If result is DNS_R_NOTFOUND:
 *		Neither the name nor a superdomain was found with data.
 *
 * Returns:
 *	DNS_R_SUCCESS		Success
 *	DNS_R_PARTIALMATCH	Superdomain found with data
 *	DNS_R_NOTFOUND		No match
 *	DNS_R_NOSPACE		Concatenating nodes to form foundname failed
 */

dns_result_t
dns_rbt_findnode(dns_rbt_t *rbt, dns_name_t *name, dns_name_t *foundname,
		 dns_rbtnode_t **node, dns_rbtnodechain_t *chain,
		 isc_boolean_t empty_data_ok, dns_rbtfindcallback_t callback,
		 void *callback_arg);
/*
 * Find the node for 'name'.
 *
 * Notes:
 *
 *	XXXRTH Changes I have made have made some of these notes inaccurate.
 *	They need to be fixed.
 *
 *	It is _not_ required that the node associated with 'name'
 *	has a non-NULL data pointer for an exact match.  A partial
 *	match must have associated data, unless the empty_data_ok flag is true.
 *
 *	If the chain parameter is non-NULL, then the path to the found
 *	node is maintained.  Within the structure, 'ancestors' will point
 *	to each successive node encountered in the search, with the root
 *	of each level searched indicated by a NULL.  ancestor_count
 *	indicates how many node pointers are in the ancestor list.  The
 *	'levels' member of the structure holds the root node of each level
 *	except the first; it corresponds with the NULL pointers in
 *	'ancestors' (except the first).  That is, for the [n+1]'th NULL
 *	'ancestors' pointer, the [n]'th 'levels' pointer is the node with
 *	the down pointer to the next level.  That node is not stored
 *	at all in the 'ancestors' list.
 *
 *	If any space was allocated to hold 'ancestors' in the chain,
 *	the 'ancestor_maxitems' member will indicate how many ancestors
 *	could have been stored; the amount to be freed from the rbt->mctx
 *	is ancestor_maxitems * sizeof(dns_rbtnode_t *).
 *
 * Requires:
 *	rbt is a valid rbt manager.
 *	dns_name_isabsolute(name) == TRUE
 *	node != NULL && *NULL == NULL
 *
 * Ensures:
 *	'name' and the tree are not altered in any way.
 *
 *	If result is DNS_R_SUCCESS:
 *		*node is the terminal node for 'name'.
 *
 * 	If result is DNS_R_PARTIALMATCH:
 *		*node is the data associated with the deepest superdomain
 * 		of 'name' which has data.
 *
 *		chain does not necessarily terminate at *node; it continues
 *		as deep as the search went.  levels[level_count - 1] is the
 *		deepest superdomain, with or without data.
 *
 *	If result is DNS_R_NOTFOUND:
 *		Neither the name nor a superdomain was found.
 *
 *		If the chain's level_count > 0, levels[level_count - 1]
 *		is the deepest partial match, regardless of whether that
 *		node had data and regardless of empty_data_ok.
 *
 *	If result is DNS_R_NOMEMORY:
 *		The function could not complete because memory could not
 *		be allocated to maintain the chain.  However, it
 *		is possible that some memory was allocated;
 *		the chain's ancestor_maxitems will be non-zero if so.
 *
 * Returns:
 *	DNS_R_SUCCESS		Success
 *	DNS_R_PARTIALMATCH	Superdomain found with data
 *	DNS_R_NOTFOUND		No match, or superdomain with no data
 *	DNS_R_NOMEMORY		Resource Limit: Out of Memory building chain
 *	DNS_R_NOSPACE		Concatenating nodes to form foundname failed
 */

dns_result_t
dns_rbt_deletename(dns_rbt_t *rbt, dns_name_t *name, isc_boolean_t recurse);
/*
 * Delete 'name' from the tree of trees.
 *
 * Notes:
 *	When 'name' is removed, if recurse is ISC_TRUE then all of its
 *      subnames are removed too.
 *
 * Requires:
 *	rbt is a valid rbt manager.
 *	dns_name_isabsolute(name) == TRUE
 *
 * Ensures:
 *	'name' is not altered in any way.
 *
 *	Does NOT ensure that any external references to nodes in the tree
 *	are unaffected by node joins.
 *
 *	If result is DNS_R_SUCCESS:
 *		'name' does not appear in the tree with data; however,
 *		the node for the name might still exist which can be
 *		found with dns_rbt_findnode (but not dns_rbt_findname).
 *
 *	If result is DNS_R_NOTFOUND:
 *		'name' does not appear in the tree with data, because
 *		it did not appear in the tree before the function was called.
 *
 *	If result is DNS_R_NOMEMORY:
 *		'name' remains in the tree, if it was there to begin with.
 *
 * Returns:
 *	DNS_R_SUCCESS	Success
 *	DNS_R_NOTFOUND	No match
 *	DNS_R_NOMEMORY	Resource Limit: Out of Memory
 */

void
dns_rbt_namefromnode(dns_rbtnode_t *node, dns_name_t *name);
/*
 * Convert the sequence of labels stored at 'node' into a 'name'.
 *
 * Notes:
 *	This function does not return the full name, from the root, but
 *	just the labels at the indicated node.
 *
 *	The name data pointed to by 'name' is the information stored
 *	in the node, not a copy.  Altering the data at this pointer
 *	will likely cause grief.
 *
 * Requires:
 *	name->offsets == NULL
 *
 * Ensures:
 *	'name' is DNS_NAMEATTR_READONLY.
 *
 *	'name' will point directly to the labels stored after the
 *	dns_rbtnode_t struct.
 *
 *	'name' will have offsets that also point to the information stored
 *	as part of the node.
 */

void
dns_rbt_destroy(dns_rbt_t **rbtp);
/*
 * Stop working with a red-black tree of trees.
 *
 * Requires:
 * 	*rbt is a valid rbt manager.
 *
 * Ensures:
 *	All space allocated by the RBT library has been returned.
 *
 *	*rbt is invalidated as an rbt manager.
 */

void
dns_rbt_printall(dns_rbt_t *rbt);
/*
 * Print an ASCII representation of the internal structure of the red-black
 * tree of trees.
 *
 * Notes:
 *	The name stored at each node, along with the node's color, is printed.
 *	Then the down pointer, left and right pointers are displayed 
 *	recursively in turn.  NULL down pointers are silently omitted;
 *	NULL left and right pointers are printed.
 */

/*****
 ***** Chain Functions
 *****/

void
dns_rbtnodechain_init(dns_rbtnodechain_t *chain, isc_mem_t *mctx);
/*
 * Initialize 'chain'.
 *
 * Requires:
 *	'chain' is a valid pointer.
 *
 *	'mctx' is a valid memory context.
 *
 * Ensures:
 *	'chain' is suitable for use.
 */

void
dns_rbtnodechain_reset(dns_rbtnodechain_t *chain);
/*
 * Free any dynamic storage associated with 'chain', and then reinitialize
 * 'chain'.
 *
 * Requires:
 *	'chain' is a valid pointer.
 *
 * Ensures:
 *	'chain' is suitable for use, and uses no dynamic storage.
 */

void
dns_rbtnodechain_invalidate(dns_rbtnodechain_t *chain);
/*
 * Free any dynamic storage associated with 'chain', and then invalidates it.
 *
 * Notes:
 * 	Future calls to any dns_rbtnodechain_ function will need to call
 * 	dns_rbtnodechain_init on the chain first (except, of course,
 *	dns_rbtnodechain_init itself).
 *
 * Requires:
 *	'chain' is a valid chain.
 *
 * Ensures:
 *	'chain' is no longer suitable for use, and uses no dynamic storage.
 */

dns_result_t
dns_rbtnodechain_current(dns_rbtnodechain_t *chain, dns_name_t *name,
			 dns_name_t *origin, dns_rbtnode_t **node);
/*
 * XXX update docs
 * Return the node to which the chain is currently pointed.
 *
 * Notes:
 *	This does not have any dependence on the tree having been locked
 *	since the chain to the node was established.
 *
 * Requires:
 *	'chain' is a valid chain.
 *
 * Ensures:
 *	The returned node is the node that was found with a prior
 *	dns_rbt_findnode, dns_rbtnodechain_first or dns_rbtnodechain_last call.
 *	If none of those functions have been called on this chain since it
 *	was initialized or rest, the return value is NULL.
 */

dns_result_t
dns_rbtnodechain_first(dns_rbtnodechain_t *chain, dns_rbt_t *rbt,
		       dns_name_t *name, dns_name_t *origin);
/*
 * Set the chain to the lexically first node in the tree of trees.
 *
 * Notes:
 *	By the definition of ordering for DNS names, the root of the tree of
 *	trees is the very first node, since everything else in the megatree uses
 *	it as a common suffix.
 *
 * Requires:
 *	'chain' is a valid chain.
 *	'name' and 'origin' are not NULL.
 *	'rbt' is a valid rbt manager.
 *
 * Ensures:
 *	'name' points to the name at the root of the tree, relative to ".".
 *	'origin' is ".".
 *
 * Returns:
 *	DNS_R_NEWORIGIN		The name & origin were successfully established.
 *	<something_else>	dns_name_concatenate failed while setting
 *				'origin' to the root name; this is its result
 *				code.
 */

dns_result_t
dns_rbtnodechain_last(dns_rbtnodechain_t *chain, dns_rbt_t *rbt,
		       dns_name_t *name, dns_name_t *origin);
/*
 * Set the chain to the lexically last node in the tree of trees.
 *
 * Requires:
 *	'chain' is a valid chain.
 *	'name' and 'origin' are not NULL.
 *	'rbt' is a valid rbt manager.
 *
 * Ensures:
 *	'name' points to the very last node of the megatree, and 'origin'
 *	is the name of the level above it.
 *
 * Returns:
 *	DNS_R_NEWORIGIN		The name & origin were successfully established.
 *	<something_else>	dns_name_concatenate failed while setting
 *				'origin' to the root name; this is its result
 *				code.
 */

dns_result_t
dns_rbtnodechain_prev(dns_rbtnodechain_t *chain, dns_name_t *name,
		      dns_name_t *origin);
/*
 *
 */

dns_result_t
dns_rbtnodechain_next(dns_rbtnodechain_t *chain, dns_name_t *name,
		      dns_name_t *origin);
/*
 *
 */


ISC_LANG_ENDDECLS

#endif /* DNS_RBT_H */
