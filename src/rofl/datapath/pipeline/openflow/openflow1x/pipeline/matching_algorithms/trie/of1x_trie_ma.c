#include "of1x_trie_ma.h"

#include <stdlib.h>
#include <assert.h>
#include "../../of1x_pipeline.h"
#include "../../of1x_flow_table.h"
#include "../../of1x_flow_entry.h"
#include "../../of1x_match.h"
#include "../../of1x_group_table.h"
#include "../../of1x_instruction.h"
#include "../../../of1x_async_events_hooks.h"
#include "../../../../../platform/lock.h"
#include "../../../../../platform/likely.h"
#include "../../../../../platform/memory.h"
#include "../../../../../util/logging.h"
#include "../matching_algorithms.h"

#define TRIE_DESCRIPTION "Trie algorithm performs the lookup using a patricia trie"

//
// Constructors and destructors
//
rofl_result_t of1x_init_trie(struct of1x_flow_table *const table){

	//Allocate main trie struct
	table->matching_aux[0] = (void*)platform_malloc_shared(sizeof(struct of1x_trie));
	of1x_trie_t* trie = (of1x_trie_t*)table->matching_aux[0];

	//Set values
	trie->root = NULL;

	return ROFL_SUCCESS;
}

//Recursively destroy leafs
static void of1x_destroy_leaf(struct of1x_trie_leaf* leaf){

	if(!leaf)
		return;

	//Destroy inner leafs
	of1x_destroy_leaf(leaf->inner);

	//Destroy next one(s)
	of1x_destroy_leaf(leaf->next);

	//Free our memory
	platform_free_shared(leaf);
}

rofl_result_t of1x_destroy_trie(struct of1x_flow_table *const table){

	of1x_trie_t* trie = (of1x_trie_t*)table->matching_aux[0];

	//Free all the leafs
	of1x_destroy_leaf(trie->root);

	//Free main leaf structure
	platform_free_shared(table->matching_aux[0]);

	return ROFL_FAILURE;
}

//
//Helper functions
//
static bool __of1x_is_tern_submatch_trie(of1x_match_t* match, of1x_flow_entry_t *const entry){
	of1x_match_t* sub_match = entry->matches.m_array[match->type];
	if(!sub_match)
		return false;
	return __of1x_is_submatch(sub_match, match);
}


//Find overlapping entries
static of1x_flow_entry_t* of1x_find_reen_trie(of1x_flow_entry_t *const entry,
								struct of1x_trie_leaf** prev,
								struct of1x_trie_leaf** next,
								bool check_overlap){
	of1x_flow_entry_t* res = NULL;
	of1x_match_t* leaf_match;
	of1x_trie_leaf_t* curr;

FIND_START:
	//Set next
	curr = *next;

	//If next is NULL and the prev has a parent, that means go down
	//in the tree. It might be that the very last leaf of the tree is matched
	//hence we are done
	if(!curr){
		if(*prev){
			//No next and parent => move back to parent and
			//process next
			curr = (*prev)->parent;
			goto FIND_NEXT;
		}else{
			goto FIND_END; //No more entries in the trie
		}
	}

	//Check itself
	leaf_match = &curr->match;
	if(check_overlap){
		//Overlap
		if( !__of1x_is_tern_submatch_trie(leaf_match, entry) &&
			 !__of1x_is_tern_submatch_trie(leaf_match, entry) )
			goto FIND_NEXT;
	}else{
		//Contained
		if( !__of1x_is_tern_submatch_trie(leaf_match, entry))
			goto FIND_NEXT;
	}

	//Matches, check if there is an entry
	if(curr->entry){
		//Set result
		res = curr->entry;

		//Set prev and next pointers
		(*prev) = curr;
		if(curr->inner)
			*next = curr->inner;
		else
			*next = curr->next;
		goto FIND_END;
	}

	//Check inner leafs
	if(curr->inner){
		//Check inner
		(*prev) = curr;
		*next = curr->inner;
		goto FIND_START;
	}

FIND_NEXT:
	(*prev) = curr;
	*next = curr->next;
	goto FIND_START;

FIND_END:
	return res;
}

//Find equal
static of1x_flow_entry_t* of1x_find_exact_trie(of1x_flow_entry_t *const entry,
								struct of1x_trie_leaf** leaf){

	//Careful when jumping to match type 1 to 2; the leaf may be an incomplete
	//match of entry and would give false "equality"

	return NULL;
}

//
// Utils
//

static bool __of1x_check_priority_cookie_trie(of1x_flow_entry_t *const entry,
						of1x_flow_entry_t *const trie_entry,
						bool check_priority,
						bool check_cookie){
	//Check cookie first
	if(check_cookie && entry->cookie != OF1X_DO_NOT_CHECK_COOKIE && entry->cookie_mask){
		if( (entry->cookie & entry->cookie_mask) != (trie_entry->cookie & entry->cookie_mask) )
			return false;
	}

	//Check priority
	if(check_priority && ((entry->priority&OF1X_2_BYTE_MASK) != (trie_entry->priority&OF1X_2_BYTE_MASK)))
		return false;

	return true;
}

static inline int __of1x_get_next_match(of1x_flow_entry_t *const entry, int curr){
	int i;
	for(i = (curr < 0)? 0 : curr; i< OF1X_MATCH_MAX; i++)
		if(entry->matches.m_array[i])
			return i;
	return -1;
}

//
//Leaf mgmt
//
static void  __of1x_create_new_branch_trie(struct of1x_trie_leaf* l,
							int m_it,
							of1x_flow_entry_t *const entry){
	struct of1x_trie_leaf *new_branch, *tmp;
	of1x_match_t* m;

	while(__of1x_get_next_match(entry, m_it) != -1){
		m_it = __of1x_get_next_match(entry, m_it);
		 = __of1x_get_alike_match(entry->matches.m_array[m_it]); 
		if(!tmp){
			assert(0);
			return NULL;
		}
		if(branch
	}

	return branch;
}


static void  __of1x_insert_new_leaf_trie(struct of1x_trie_leaf l,
							int m_it,
							of1x_flow_entry_t *const entry){

	struct of1x_trie_leaf* new_branch;

	new_branch = 

	//We append as next
	l->next = 
}

rofl_of1x_fm_result_t __of1x_add_leafs_trie(of1x_trie_t* trie,
							of1x_flow_entry_t *const entry){

	int m_it;
	struct of1x_trie_leaf *l, *l_prev;
	struct of1x_trie_leaf *tmp, *branch;
	of1x_match_t* m;

	//Determine entry's first match
	m_it = __of1x_get_next_match(entry, -1);

	if(m_it == -1){
		//This is an empty entry, therefore it needs to be
		//added in the root of the tree
		//FIXME TODO XXX
	}

	//Start by the very first leaf
	l = trie->root;
	l_prev = NULL;
	m = entry->matches.m_array[m_it];

	//Determine the point of insertion
	while(l){
		//Check if they share something
		if(!__of1x_is_alike_match(m, &l->match)){
			if(!l->next){
				//This is the point of insertion
				__of1x_insert_new_leaf_trie(l, m_it, m, entry);
				break;
			}

			l_prev = l;
			l = l->next;
			continue;
		}

		//
		// We have to follow this branch
		//

		//If they do and the leaf is not a submatch, then
		//this is the point of insertion
		if(!__of1x_is_submatch(m, &l->match)){
			__of1x_insert_intermediate_leaf_trie(l, m_it, m, entry);
			goto ADD_LEAFS_END;
		}

		//If it is equal, then we have to move to the next match
		//or stop here if it is the last
		if(__of1x_equal_matches(m, &l->match)){
			//Increment match
			if(__of1x_get_next_match(entry, m_it) == -1){
				//No more matches; this is the insertion point
				__of1x_insert_terminal_leaf_trie(l, m_it, m, entry);
				break;
			}
			m_it = __of1x_get_next_match(entry, m_it);
			m = entry->matches.m_array[m_it];
		}

		//We have to go deeper
		l_prev = l;
		l = l->inner;
	}

	assert(0);
	return ROFL_OF1X_FM_FAILURE;

ADD_LEAFS_END:

	return ROFL_OF1X_FM_SUCCESS;
}

//
// Main routines
//
rofl_of1x_fm_result_t of1x_add_flow_entry_trie(of1x_flow_table_t *const table,
								of1x_flow_entry_t *const entry,
								bool check_overlap,
								bool reset_counts){

	rofl_of1x_fm_result_t res = ROFL_OF1X_FM_SUCCESS;
	of1x_trie_t* trie = (of1x_trie_t*)table->matching_aux[0];
	struct of1x_trie_leaf *prev, *next;
	of1x_flow_entry_t* curr_entry;

	//Allow single add/remove operation over the table
	platform_mutex_lock(table->mutex);

	//Check overlap
	if(check_overlap){
		//Point to the root of the tree
		prev = NULL;
		next = trie->root;
		curr_entry = of1x_find_reen_trie(entry, &prev, &next, true);

		//Check cookie and priority
		if(curr_entry && __of1x_check_priority_cookie_trie(entry,
									curr_entry,
									true,
									false)){
			res = ROFL_OF1X_FM_OVERLAP;
			ROFL_PIPELINE_ERR("[flowmod-add(%p)][trie] Overlaps with another entry (%p)\n", entry, curr_entry);
			goto ADD_END;
		}
	}

	//Point to the root of the tree
	prev = NULL;
	next = trie->root;

	//Check existent (there can be only one, but there can be multiple ones chained under "next"
	//poitner (different priorities)
	curr_entry = of1x_find_exact_trie(entry, &next);

	while(curr_entry){
		if(curr_entry && __of1x_check_priority_cookie_trie(entry, curr_entry,
										true,
										false)){
			ROFL_PIPELINE_DEBUG("[flowmod-modify(%p)][trie] Existing entry (%p) will be updated with (%p)\n", entry, curr_entry, entry);

			next->entry = entry;
			//FIXME: copy stats
			//FIXME: remove curr_entry and update

			if(!curr_entry->next)
				goto ADD_END;
		}
	}

	//Point to the root of the tree
	prev = NULL;
	next = trie->root;

	//If we arrived in here, we have to add the entry (no existing entries)
	res = __of1x_add_leafs_trie(trie, entry);

ADD_END:
	platform_mutex_unlock(table->mutex);
	return res;
}

rofl_result_t of1x_modify_flow_entry_trie(of1x_flow_table_t *const table,
						of1x_flow_entry_t *const entry,
						const enum of1x_flow_removal_strictness strict,
						bool reset_counts){
	return ROFL_FAILURE;
}

rofl_result_t of1x_remove_flow_entry_trie(of1x_flow_table_t *const table,
						of1x_flow_entry_t *const entry,
						of1x_flow_entry_t *const specific_entry,
						const enum of1x_flow_removal_strictness strict,
						uint32_t out_port,
						uint32_t out_group,
						of1x_flow_remove_reason_t reason,
						of1x_mutex_acquisition_required_t mutex_acquired){
	return ROFL_FAILURE;
}

rofl_result_t of1x_get_flow_stats_trie(struct of1x_flow_table *const table,
		uint64_t cookie,
		uint64_t cookie_mask,
		uint32_t out_port, 
		uint32_t out_group,
		of1x_match_group_t *const matches,
		of1x_stats_flow_msg_t* msg){

	return ROFL_FAILURE;
}

rofl_result_t of1x_get_flow_aggregate_stats_trie(struct of1x_flow_table *const table,
		uint64_t cookie,
		uint64_t cookie_mask,
		uint32_t out_port, 
		uint32_t out_group,
		of1x_match_group_t *const matches,
		of1x_stats_flow_aggregate_msg_t* msg){

	return ROFL_FAILURE;
}

of1x_flow_entry_t* of1x_find_entry_using_group_trie(of1x_flow_table_t *const table,
		const unsigned int group_id){

	return NULL;
}

void of1x_dump_trie(struct of1x_flow_table *const table, bool raw_nbo){

}


//Define the matching algorithm struct
OF1X_REGISTER_MATCHING_ALGORITHM(trie) = {
	//Init and destroy hooks
	.init_hook = of1x_init_trie,
	.destroy_hook = of1x_destroy_trie,

	//Flow mods
	.add_flow_entry_hook = of1x_add_flow_entry_trie,
	.modify_flow_entry_hook = of1x_modify_flow_entry_trie,
	.remove_flow_entry_hook = of1x_remove_flow_entry_trie,

	//Stats
	.get_flow_stats_hook = of1x_get_flow_stats_trie,
	.get_flow_aggregate_stats_hook = of1x_get_flow_aggregate_stats_trie,

	//Find group related entries
	.find_entry_using_group_hook = of1x_find_entry_using_group_trie,

	//Dumping
	.dump_hook = of1x_dump_trie,
	.description = TRIE_DESCRIPTION,
};
