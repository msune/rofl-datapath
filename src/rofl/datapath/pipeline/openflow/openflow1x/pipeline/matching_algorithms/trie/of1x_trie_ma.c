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
	trie->entry = NULL;
	trie->root = NULL;

	return ROFL_SUCCESS;
}

//Recursively destroy leafs
static void of1x_destroy_leaf(struct of1x_trie_leaf* leaf){

	of1x_flow_entry_t *entry, *tmp;

	if(!leaf)
		return;

	//Destroy entry/ies
	entry = leaf->entry;
	while(entry){
		tmp = entry->next;
		of1x_destroy_flow_entry(entry);
		entry = tmp;
	}

	//Destroy inner leafs
	of1x_destroy_leaf(leaf->inner);

	//Destroy next one(s)
	of1x_destroy_leaf(leaf->next);

	//Free our memory
	platform_free_shared(leaf);
}

rofl_result_t of1x_destroy_trie(struct of1x_flow_table *const table){

	of1x_flow_entry_t *entry, *tmp;
	of1x_trie_t* trie = (of1x_trie_t*)table->matching_aux[0];

	//Free all the leafs
	of1x_destroy_leaf(trie->root);

	//Destroy entry/ies
	entry = trie->entry;
	while(entry){
		tmp = entry->next;
		of1x_destroy_flow_entry(entry);
		entry = tmp;
	}

	//Free main leaf structure
	platform_free_shared(table->matching_aux[0]);

	return ROFL_FAILURE;
}

//
//Linked-list
//
void __of1x_add_ll_prio_trie(of1x_flow_entry_t** head, of1x_flow_entry_t* entry){

	of1x_flow_entry_t *tmp;

	if(!head){
		assert(0);
		return;
	}

	//No head
	if(*head == NULL){
		entry->prev = entry->next = NULL;
		*head = entry;
		return;
	}

	//Loop over the entries and find the right position
	tmp = *head;
	while(tmp){
		//Insert before tmp
		if(tmp->priority < entry->priority){
			entry->prev = tmp->prev;
			entry->next = tmp;
			if(tmp->prev)
				tmp->prev->next = entry;
			else
				*head = entry;
			tmp->prev = entry;
			return;
		}
		//If it is the last one insert in the tail
		//and return
		if(!tmp->next){
			tmp->next = entry;
			entry->prev = tmp;
			entry->next = NULL;
			return;
		}
		tmp = tmp->next;
	}

	//Cannot reach this point
	assert(0);
}

void __of1x_remove_ll_prio_trie(of1x_flow_entry_t** head, of1x_flow_entry_t* entry){

	if(!head || *head == NULL){
		assert(0);
		return;
	}

	if(entry == *head){
		//It is the head
		*head = entry->next;
		if(entry->next)
			entry->next->prev = NULL;
	}else{
		//This is used during matching
		entry->prev->next = entry->next;

		//This is not
		entry->next->prev = entry->prev;
	}
}

//
//Helper functions
//
static bool __of1x_is_tern_submatch_trie(of1x_match_t* match,
							of1x_match_group_t *const matches){
	of1x_match_t* sub_match = matches->m_array[match->type];
	if(!sub_match)
		return false;
	return __of1x_is_submatch(sub_match, match);
}

static bool __of1x_is_tern_complete_match_trie(of1x_match_t* match,
							of1x_match_group_t *const matches){
	of1x_match_t* mg = matches->m_array[match->type];
	of1x_match_t alike_match;

	if(__of1x_get_alike_match(match, mg, &alike_match))
		return false;
	return __of1x_equal_matches(match, &alike_match);
}

//Find overlapping entries
static of1x_flow_entry_t* of1x_find_reen_trie( of1x_match_group_t *const matches,
								struct of1x_trie_leaf** prev,
								struct of1x_trie_leaf** next,
								bool check_overlap,
								bool check_complete){
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
		if(*prev && (*prev)->parent){
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
		if( !__of1x_is_tern_submatch_trie(leaf_match, matches) &&
			 !__of1x_is_tern_submatch_trie(leaf_match, matches) )
			goto FIND_NEXT;
	}else{
		//Contained
		if( !__of1x_is_tern_submatch_trie(leaf_match, matches))
			goto FIND_NEXT;
	}

	//Matches, check if there is an entry
	if(curr->entry){
		if(check_complete){
			//If check_complete is 1, we have to make sure
			//we match completely and not partially the match
			if(!__of1x_is_tern_complete_match_trie(leaf_match, matches))
				goto FIND_NEXT;
		}

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
		if(check_complete){
			//If check_complete is 1, we have to make sure
			//we match completely and not partially the match
			if(leaf_match->type != curr->inner->match.type &&
				! __of1x_is_tern_complete_match_trie(leaf_match, matches))
				goto FIND_NEXT;
		}
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
	for(i = (curr < 0)? 0 : (curr+1); i< OF1X_MATCH_MAX; i++)
		if(entry->matches.m_array[i])
			return i;
	return -1;
}

//
//Leaf mgmt
//

static of1x_trie_leaf_t*  __of1x_create_new_branch_trie(int m_it, of1x_flow_entry_t *const entry){

	of1x_trie_leaf_t *new_branch = NULL;
	of1x_trie_leaf_t *last_leaf = NULL;
	of1x_trie_leaf_t *tmp;
	of1x_match_t* m;

	while(m_it != -1){
		m = entry->matches.m_array[m_it];

		//Allocate space
		tmp = (of1x_trie_leaf_t*)platform_malloc_shared(sizeof(of1x_trie_leaf_t));
		if(!tmp){
			//Out of memory
			assert(0);
			return NULL;
		}

		//Init
		memset(tmp, 0, sizeof(of1x_trie_leaf_t));

		//Copy match (cannot fail)
		if(!__of1x_get_alike_match(m, m, &tmp->match)){
			assert(0);
			return NULL;
		}

		//Fill-in
		if(last_leaf){
			last_leaf->entry = NULL;
			last_leaf->next = last_leaf->prev = NULL;
			last_leaf->inner_max_priority = entry->priority;
		}

		//Set the linked list
		if(!last_leaf){
			new_branch = last_leaf = tmp;
		}else{
			last_leaf->inner = tmp;
			tmp->parent = last_leaf;
			last_leaf = tmp;
		}
		m_it = __of1x_get_next_match(entry, m_it);
	}

	last_leaf->entry = entry;

	return new_branch;
}


//Append to the "next"
static rofl_of1x_fm_result_t __of1x_insert_next_leaf_trie(struct of1x_trie_leaf* l,
							int m_it,
							of1x_flow_entry_t *const entry){

	struct of1x_trie_leaf* new_branch;
	struct of1x_trie_leaf* l_poi;

	//Get the new branch
	new_branch = __of1x_create_new_branch_trie(m_it, entry);

	if(!new_branch)
		return ROFL_OF1X_FM_FAILURE;

	//Fill-in missing information
	new_branch->parent = l->parent;
	new_branch->prev = l;

	/*
	* Adjust inner max priority
	*/

	//First adjust our own group head
	if(l->parent && (l->parent->inner->inner_max_priority < entry->priority)){
		l->parent->inner->inner_max_priority = entry->priority;
	}

	//Then go recursively down
	l_poi = l;
	l = l->parent;
	while(l){
		if(l->inner_max_priority < entry->priority)
			l->inner_max_priority = entry->priority;
		else
			break;
		l = l->parent;
	}

	//We append as next
	entry->prev = entry->next = NULL;
	l_poi->next = new_branch;

	return ROFL_OF1X_FM_SUCCESS;
}

//Add an intermediate match and branch from that point
static rofl_of1x_fm_result_t __of1x_insert_intermediate_leaf_trie(of1x_trie_t* trie,
							struct of1x_trie_leaf* l,
							int m_it,
							of1x_flow_entry_t *const entry){
	of1x_trie_leaf_t *intermediate;
	struct of1x_trie_leaf* new_branch;
	of1x_match_t* m;

	m = entry->matches.m_array[m_it];

	//Allocate space
	intermediate = (of1x_trie_leaf_t*)platform_malloc_shared(sizeof(of1x_trie_leaf_t));
	if(!intermediate){
		//Out of memory
		assert(0);
		return ROFL_OF1X_FM_FAILURE;
	}

	//Get the common part
	if(!__of1x_get_alike_match(&l->match, m, &intermediate->match)){
		//Can never (ever) happen
		assert(0);
		return ROFL_OF1X_FM_FAILURE;
	}

	//Get the new branch
	new_branch = __of1x_create_new_branch_trie(m_it, entry);

	if(!new_branch)
		return ROFL_OF1X_FM_FAILURE;


	/*
	* Fill in the intermediate and new branch
	*/

	//Intermediate leaf
	intermediate->inner = l;
	intermediate->prev = intermediate->next = NULL;
	intermediate->parent = l->parent;

	//new branch
	new_branch->parent = intermediate;
	new_branch->prev = l;
	new_branch->next = NULL;

	//Previous leaf, now child
	l->parent = intermediate;
	l->next = new_branch;

	//Entry linked list
	entry->prev = entry->next = NULL;

	/*
	* Adjust inner_max_priority
	*/
	if(l->inner_max_priority < entry->priority){
		//Adjust intermediate
		intermediate->inner_max_priority = entry->priority;

		//Recursively adjust
		l = intermediate->parent;
		while(l){
			if(l->inner_max_priority < entry->priority)
				l->inner_max_priority = entry->priority;
			else
				break;
			l = l->parent;
		}
	}else{
		intermediate->inner_max_priority = l->inner_max_priority;
	}

	//Now insert
	if(intermediate->parent)
		intermediate->parent->inner = intermediate;
	else
		trie->root = intermediate;

	return ROFL_OF1X_FM_SUCCESS;
}

static rofl_of1x_fm_result_t __of1x_insert_terminal_leaf_trie(struct of1x_trie_leaf* l,
							int m_it,
							of1x_flow_entry_t *const entry){
	uint32_t max_priority = entry->priority;

	//l is the terminal leaf (entry has no more matches)
	if(l->entry){
		//Append to the very last in the linked list
		__of1x_add_ll_prio_trie(&l->entry, entry);
	}else{
		l->entry = entry;
		entry->prev = NULL;
	}
	entry->next = NULL;

	/*
	* Adjust inner_max_priority
	*/
	max_priority = (max_priority > l->inner_max_priority)? max_priority : l->inner_max_priority;
	while(l){
		if(l->inner_max_priority < max_priority)
			l->inner_max_priority = max_priority;
		else
			break;
		l = l->parent;
	}

	return ROFL_OF1X_FM_SUCCESS;
}

//Perform the real addition to the trie
rofl_of1x_fm_result_t __of1x_add_leafs_trie(of1x_trie_t* trie,
							of1x_flow_entry_t *const entry){

	int m_it;
	struct of1x_trie_leaf *l;
	of1x_match_t *m, tmp;
	rofl_of1x_fm_result_t res = ROFL_OF1X_FM_SUCCESS;

	//Determine entry's first match
	m_it = __of1x_get_next_match(entry, -1);

	if(m_it == -1){
		//This is an empty entry, therefore it needs to be
		//added in the root of the tree
		__of1x_add_ll_prio_trie(&trie->entry, entry);
		goto ADD_LEAFS_END;
	}

	//Start by the very first leaf
	l = trie->root;
	m = entry->matches.m_array[m_it];

	if(!l){
		//This is the point of insertion
		trie->root = __of1x_create_new_branch_trie(m_it, entry);
		if(!trie->root)
			res = ROFL_OF1X_FM_FAILURE;
		goto ADD_LEAFS_END;
	}

	//Determine the point of insertion
	while(l){
		//Check if they share something
		if(__of1x_is_submatch(m, &l->match) == false &&
			__of1x_get_alike_match(m, &l->match, &tmp) == false){
			if(!l->next){
				//This is the point of insertion
				res = __of1x_insert_next_leaf_trie(l, m_it, entry);
				goto ADD_LEAFS_END;
			}

			l = l->next;
			continue;
		}

		/*
		* We have to follow this branch
		*/

		//If it is equal, then we have to move to the next match
		//or stop here if it is the last
		if(__of1x_equal_matches(&l->match, &tmp)){
			//Increment match
			if(__of1x_get_next_match(entry, m_it) == -1){
				//No more matches; this is the insertion point
				res = __of1x_insert_terminal_leaf_trie(l, m_it, entry);
				goto ADD_LEAFS_END;
			}
			m_it = __of1x_get_next_match(entry, m_it);
			m = entry->matches.m_array[m_it];
		}else{
			//This is the point of insertion
			res = __of1x_insert_intermediate_leaf_trie(trie, l, m_it,
										entry);
			goto ADD_LEAFS_END;
		}

		//We have to go deeper
		l = l->inner;
	}

	//We shall never reach this point
	assert(0);
	return ROFL_OF1X_FM_FAILURE;

ADD_LEAFS_END:

	return res;
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
	of1x_flow_entry_t *curr_entry, *to_be_removed=NULL, **ll_head;

	//Allow single add/remove operation over the table
	platform_mutex_lock(table->mutex);

	/*
	* Check overlap
	*/

	if(check_overlap){
		//Point to the root of the tree
		prev = NULL;
		next = trie->root;

		//No match entries
		curr_entry = trie->entry;

		do{
			//Get next overlapping
			if(!curr_entry){
				curr_entry = of1x_find_reen_trie(&entry->matches, &prev,
										&next,
										true,
										false);

				//If no more entries are found, we are done
				if(!curr_entry)
					break;
			}

			//Check cookie and priority
			if(curr_entry && __of1x_check_priority_cookie_trie(entry,
										curr_entry,
										true,
										false)){
				res = ROFL_OF1X_FM_OVERLAP;
				ROFL_PIPELINE_ERR("[flowmod-add(%p)][trie] Overlaps with, at least, another entry (%p)\n", entry, curr_entry);
				goto ADD_END;
			}
			curr_entry = curr_entry->next;
		}while(1);
	}

	/*
	* Check exact (overwrite)
	*/

	//Point to the root of the tree
	prev = NULL;
	next = trie->root;

	//No match entries
	curr_entry = trie->entry;

	do{
		//Get next exact
		if(!curr_entry){
			//Check existent (they can only be in this position of the trie,
			//but there can be multiple ones chained under "next" pointer (different priorities)
			curr_entry = of1x_find_reen_trie(&entry->matches, &prev,
									&next,
									true,
									true);
			//If no more entries are found, we are done
			if(!curr_entry)
				break;
		}

		if(curr_entry && __of1x_check_priority_cookie_trie(entry, curr_entry,
										true,
										false)){
			ROFL_PIPELINE_DEBUG("[flowmod-modify(%p)][trie] Existing entry (%p) will be updated with (%p)\n", entry, curr_entry, entry);

			//Head of the entry linked list
			if(prev)
				ll_head = &prev->entry;
			else
				ll_head = &trie->entry;

			//Add the new entry
			__of1x_add_ll_prio_trie(ll_head, entry);

			//Remove the previous
			__of1x_remove_ll_prio_trie(ll_head, curr_entry);

			//Mark the entry to be removed
			to_be_removed = curr_entry;

			//Update stats
			if(!reset_counts){
				__of1x_stats_flow_tid_t c;

#ifdef ROFL_PIPELINE_LOCKLESS
				tid_wait_all_not_present(&table->tid_presence_mask);
#endif

				//Consolidate stats
				__of1x_stats_flow_consolidate(&curr_entry->stats, &c);

				//Add; note that position 0 is locked by us
				curr_entry->stats.s.__internal[0].packet_count += c.packet_count;
				curr_entry->stats.s.__internal[0].byte_count += c.byte_count;
			}

			goto ADD_END;
		}
		curr_entry = curr_entry->next;
	}while(1);

	//If we got in here, we have to add the entry (no existing entries)
	res = __of1x_add_leafs_trie(trie, entry);
	table->num_of_entries++;

ADD_END:
	if(to_be_removed){
#ifdef ROFL_PIPELINE_LOCKLESS
		tid_wait_all_not_present(&table->tid_presence_mask);
#endif
		of1x_destroy_flow_entry(to_be_removed);
	}

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
