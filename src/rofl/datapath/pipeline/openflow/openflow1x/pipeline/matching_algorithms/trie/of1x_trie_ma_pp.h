#ifndef __OF1X_TRIE_MATCH_PP_H__
#define __OF1X_TRIE_MATCH_PP_H__

#include "rofl_datapath.h"
#include "../../of1x_pipeline.h"
#include "../../of1x_flow_table.h"
#include "../../of1x_flow_entry.h"
#include "../../of1x_match_pp.h"
#include "../../of1x_group_table.h"
#include "../../of1x_instruction_pp.h"
#include "../../../of1x_async_events_hooks.h"
#include "../../../../../platform/lock.h"
#include "../../../../../platform/likely.h"
#include "../../../../../platform/memory.h"
#include "of1x_trie_ma.h"

//C++ extern C
ROFL_BEGIN_DECLS

// NOTE: this can never be inlined. Just putting it here
static inline void of1x_check_leaf_trie(datapacket_t *const pkt,
					struct of1x_trie_leaf* leaf,
					int64_t* match_priority,
					of1x_flow_entry_t** best_match){

	//Check inner
	if(((int64_t)leaf->inner_max_priority) > *match_priority){
		//Check match
		if(__of1x_check_match(pkt, &leaf->match)){
			if(leaf->entry)
				*best_match = leaf->entry;
			if(leaf->inner)
				of1x_check_leaf_trie(pkt,
							leaf->inner,
							match_priority,
							best_match);
		}
	}

	if(leaf->next)
		of1x_check_leaf_trie(pkt, leaf->next,
							match_priority,
							best_match);
}


/* FLOW entry lookup entry point */
static inline of1x_flow_entry_t* of1x_find_best_match_trie_ma(of1x_flow_table_t *const table,
							datapacket_t *const pkt){

	of1x_flow_entry_t* best_match = NULL;
	int64_t match_priority = -1;
	struct of1x_trie* trie = ((of1x_trie_t*)table->matching_aux[0]);
	struct of1x_trie_leaf* leaf = trie->root;

	//TODO add rwlock / fence

	//Entries with no matches
	if(trie->entry){
		best_match = trie->entry;
		match_priority = best_match->priority;
	}

	//Start recursion
	of1x_check_leaf_trie(pkt, leaf, &match_priority, &best_match);

	return best_match;
}

//C++ extern C
ROFL_END_DECLS

#endif //OF1X_TRIE_MATCH_PP
