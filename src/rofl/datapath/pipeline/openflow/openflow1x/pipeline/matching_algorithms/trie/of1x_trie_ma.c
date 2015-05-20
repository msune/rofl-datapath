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

#if 0
//
//Helper functions
//
static inline bool of1x_is_tern_match(of1x_match_t* match, of1x_flow_entry_t *const entry){
	of1x_match_t* sub_match = entry->matches.m_array[match->type];
	if(!sub_match)
		return false;
	return __of1x_is_submatch(sub_match, match);
}
#endif


//Find overlapping entries
static of1x_flow_entry_t* of1x_find_overlap_reen_trie(of1x_flow_entry_t *const entry,
								struct of1x_trie_leaf** prev,
								struct of1x_trie_leaf** next){
	struct of1x_trie_leaf* curr = *next;
	of1x_flow_entry_t* res = NULL;

	if(curr == (*prev)->parent){
		*next = (*prev)->parent;
		return of1x_find_overlap_reen_trie(entry, NULL, next);
	}

	//Check itself
	if(curr->entry){
		res = curr->entry;
		goto FOUND_OVERLAP;
	}

	//Check the inner
FOUND_OVERLAP:

	return res;
}

#if 0
//Find contained
static of1x_flow_entry_t* of1x_find_contained_reen_trie(of1x_flow_entry_t *const entry,
								struct of1x_trie_leaf** prev,
								struct of1x_trie_leaf** next){

	return NULL;
}
#endif

//Find equal
static of1x_flow_entry_t* of1x_find_exact_trie(of1x_flow_entry_t *const entry,
								struct of1x_trie_leaf** leaf){

	//Careful when jumping to match type 1 to 2; the leaf may be an incomplete
	//match of entry and would give false "equality"

	return NULL;
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
		if(of1x_find_overlap_reen_trie(entry, &prev, &next) != NULL){
			res = ROFL_OF1X_FM_OVERLAP;
			goto ADD_END;
		}
	}

	//Point to the root of the tree
	prev = NULL;
	next = trie->root;

	//Check existent
	curr_entry = of1x_find_exact_trie(entry, &next);
	if(curr_entry){
		ROFL_PIPELINE_DEBUG("[trie][flowmod-modify(%p)] Existing entry (%p) will be updated with (%p)\n", entry, curr_entry, entry);
		if(__of1x_update_flow_entry(curr_entry, entry, reset_counts) != ROFL_SUCCESS){
			res = ROFL_OF1X_FM_FAILURE;
			goto ADD_END;
		}
		goto ADD_END;
	}

	//Point to the root of the tree
	prev = NULL;
	next = trie->root;

	//If we arrived in here, we have to add the entry (no existing entries)

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
	.dump_hook = NULL,
	.description = TRIE_DESCRIPTION,
};
