#include "of12_flow_entry.h"

#include "../../../platform/memory.h"
#include "../of12_endpoint_hooks.h"

#include <stdio.h>
#include <assert.h>
#include "../of12_switch.h"
#include "of12_pipeline.h"
#include "of12_flow_table.h"
#include "of12_action.h"
#include "of12_group_table.h"

/*
* Intializer and destructor
*/

of12_flow_entry_t* of12_init_flow_entry(of12_flow_entry_t* prev, of12_flow_entry_t* next, bool notify_removal){

	of12_flow_entry_t* entry = (of12_flow_entry_t*)platform_malloc_shared(sizeof(of12_flow_entry_t));
	
	if(!entry)
		return NULL;

	memset(entry,0,sizeof(of12_flow_entry_t));	
	
	if(NULL == (entry->rwlock = platform_rwlock_init(NULL))){
		platform_free_shared(entry);
		assert(0);
		return NULL; 
	}
	
	//Init linked list
	entry->prev = prev;
	entry->next = next;
	
	of12_init_instruction_group(&entry->inst_grp);

	//init stats
	of12_init_flow_stats(entry);

	//Flags
	entry->notify_removal = notify_removal;
	
	return entry;	

}


//This function is meant to only be used internally
rofl_result_t of12_destroy_flow_entry_with_reason(of12_flow_entry_t* entry, of12_flow_remove_reason_t reason){
	
	of12_match_t* match = entry->matchs;

	//wait for any thread which is still using the entry (processing a packet)
	platform_rwlock_wrlock(entry->rwlock);
	
	//destroying timers, if any
	of12_destroy_timer_entries(entry);

	//Notify flow removed
	if(entry->notify_removal && (reason != OF12_FLOW_REMOVE_NO_REASON ) ){
		//Safety checks
		if(entry->table && entry->table->pipeline && entry->table->pipeline->sw)
			platform_of12_notify_flow_removed(entry->table->pipeline->sw, reason, entry);	
			
	}	

	//destroy stats
	of12_destroy_flow_stats(entry);

	//Destroy matches recursively
	while(match){
		of12_match_t* next = match->next;
		//TODO: maybe check result of destroy and print traces...
		of12_destroy_match(match);
		match = next; 
	}

	//Destroy instructions
	of12_destroy_instruction_group(&entry->inst_grp);
	
	platform_rwlock_destroy(entry->rwlock);
	
	//Destroy entry itself
	platform_free_shared(entry);	
	
	return ROFL_SUCCESS;
}

//This is the interface to be used when deleting entries used as
//a message or not inserted in a table 
rofl_result_t of12_destroy_flow_entry(of12_flow_entry_t* entry){
	return of12_destroy_flow_entry_with_reason(entry, OF12_FLOW_REMOVE_NO_REASON);	
}

//Adds one or more to the entry
rofl_result_t of12_add_match_to_entry(of12_flow_entry_t* entry, of12_match_t* match){

	unsigned int new_matches;

	if(!match)
		return ROFL_FAILURE;

	if(entry->matchs){
		of12_add_match(entry->matchs, match);		
	}else{
		entry->matchs = match;

		//Make sure is correctly formed
		match->prev = NULL;

		//Set the number of matches
		entry->num_of_matches=0;
	}

	//Determine number of new matches.
	for(new_matches=0;match;match=match->next,new_matches++);

	entry->num_of_matches+=new_matches;

	return ROFL_SUCCESS;
}

rofl_result_t of12_update_flow_entry(of12_flow_entry_t* entry_to_update, of12_flow_entry_t* mod, bool reset_counts){


	//Lock entry
	platform_rwlock_wrlock(entry_to_update->rwlock);

	//Copy instructions
	of12_update_instructions(&entry_to_update->inst_grp, &mod->inst_grp);

	//Reset counts
	if(reset_counts)
		of12_stats_flow_reset_counts(entry_to_update);

	//Unlock
	platform_rwlock_wrunlock(entry_to_update->rwlock);

	//Destroy the mod entry
	of12_destroy_flow_entry_with_reason(mod, OF12_FLOW_REMOVE_NO_REASON);

	return ROFL_SUCCESS;
}
/**
* Checks whether two entries overlap overlapping. This is potentially an expensive call.
* Try to avoid using it, if the matching algorithm can guess via other (more efficient) ways...
*/
bool of12_flow_entry_check_overlap(of12_flow_entry_t*const original, of12_flow_entry_t*const entry, bool check_priority, bool check_cookie, uint32_t out_port, uint32_t out_group){

	of12_match_t* it_orig, *it_entry;
	
	//Check cookie first
	if(check_cookie && entry->cookie){
		if( (entry->cookie&entry->cookie_mask) == (original->cookie&entry->cookie_mask) )
			return false;
	}

	//Check priority
	if(check_priority && (entry->priority != original->priority))
		return false;

	//Check if matchs are contained. This is expensive.. //FIXME: move this to of12_match
	for( it_entry = entry->matchs; it_entry; it_entry = it_entry->next ){
		for( it_orig = original->matchs; it_orig; it_orig = it_orig->next ){

			//Skip if different types
			if( it_entry->type != it_orig->type)
				continue;	
			
			if( !of12_is_submatch( it_entry, it_orig ) && !of12_is_submatch( it_orig, it_entry ) )
				return false;
		}
	}


	//Check out group actions
	if( out_group != OF12_GROUP_ANY && ( 
			!of12_write_actions_has(entry->inst_grp.instructions[OF12_IT_WRITE_ACTIONS].write_actions, OF12_AT_GROUP, out_group) &&
			!of12_apply_actions_has(entry->inst_grp.instructions[OF12_IT_APPLY_ACTIONS].apply_actions, OF12_AT_GROUP, out_group)
			)
	)
		return false;


	//Check out port actions
	if( out_port != OF12_PORT_ANY && ( 
			!of12_write_actions_has(entry->inst_grp.instructions[OF12_IT_WRITE_ACTIONS].write_actions, OF12_AT_OUTPUT, out_port) &&
			!of12_apply_actions_has(entry->inst_grp.instructions[OF12_IT_APPLY_ACTIONS].apply_actions, OF12_AT_OUTPUT, out_port)
			)
	)
		return false;

	return true;
}

/**
* Checks whether an entry is contained in the other. This is potentially an expensive call.
* Try to avoid using it, if the matching algorithm can guess via other (more efficient) ways...
*/
bool of12_flow_entry_check_contained(of12_flow_entry_t*const original, of12_flow_entry_t*const subentry, bool check_priority, bool check_cookie, uint32_t out_port, uint32_t out_group){

	of12_match_t* it_orig, *it_subentry;
	
	//Check cookie first
	if(check_cookie && subentry->cookie){
		if( (subentry->cookie&subentry->cookie_mask) == (original->cookie&subentry->cookie_mask) )
			return false;
	}

	//Check priority
	if(check_priority && (original->priority != subentry->priority))
		return false;

	//Check if matchs are contained. This is expensive.. //FIXME: move this to of12_match
	for( it_subentry = subentry->matchs; it_subentry; it_subentry = it_subentry->next ){
		for( it_orig = original->matchs; it_orig; it_orig = it_orig->next ){
	
			//Skip if different types
			if( it_subentry->type != it_orig->type)
				continue;	
			
			if( !of12_is_submatch( it_subentry, it_orig ) )
				return false;
		}
	}

	//Check out group actions
	if( out_group != OF12_GROUP_ANY && ( 
			!of12_write_actions_has(original->inst_grp.instructions[OF12_IT_WRITE_ACTIONS].write_actions, OF12_AT_GROUP, out_group) &&
			!of12_apply_actions_has(original->inst_grp.instructions[OF12_IT_APPLY_ACTIONS].apply_actions, OF12_AT_GROUP, out_group)
			)
	)
		return false;


	//Check out port actions
	if( out_port != OF12_PORT_ANY && ( 
			!of12_write_actions_has(original->inst_grp.instructions[OF12_IT_WRITE_ACTIONS].write_actions, OF12_AT_OUTPUT, out_port) &&
			!of12_apply_actions_has(original->inst_grp.instructions[OF12_IT_APPLY_ACTIONS].apply_actions, OF12_AT_OUTPUT, out_port)
			)
	)
		return false;


	return true;
}
/**
* Checks if entry is identical to another one
*/
bool of12_flow_entry_check_equal(of12_flow_entry_t*const original, of12_flow_entry_t*const entry, uint32_t out_port, uint32_t out_group){

	of12_match_t* it_original, *it_entry;
	
	//Check cookie first
	if(entry->cookie){
		if( (entry->cookie&entry->cookie_mask) == (original->cookie&entry->cookie_mask) )
			return false;
	}

	//Check priority
	if(entry->priority != original->priority)
		return false;

	//Fast Check #matchs
	if(original->num_of_matches != entry->num_of_matches) 
		return false;

	//Matches in-depth check //FIXME: move this to of12_match
	for(it_original = original->matchs, it_entry = entry->matchs; it_entry != NULL; it_original = it_original->next, it_entry = it_entry->next){	
		if(!of12_equal_matches(it_original,it_entry))
			return false;
	}

	//Check out group actions
	if( out_group != OF12_GROUP_ANY && ( 
			!of12_write_actions_has(original->inst_grp.instructions[OF12_IT_WRITE_ACTIONS].write_actions, OF12_AT_GROUP, out_group) &&
			!of12_apply_actions_has(original->inst_grp.instructions[OF12_IT_APPLY_ACTIONS].apply_actions, OF12_AT_GROUP, out_group)
			)
	)
		return false;


	//Check out port actions
	if( out_port != OF12_PORT_ANY && ( 
			!of12_write_actions_has(original->inst_grp.instructions[OF12_IT_WRITE_ACTIONS].write_actions, OF12_AT_OUTPUT, out_port) &&
			!of12_apply_actions_has(original->inst_grp.instructions[OF12_IT_APPLY_ACTIONS].apply_actions, OF12_AT_OUTPUT, out_port)
			)
	)
		return false;


	return true;
}

void of12_dump_flow_entry(of12_flow_entry_t* entry){
	fprintf(stderr,"Entry (%p), #hits %u prior. %u",entry,entry->num_of_matches, entry->priority);
	//print matches(all)
	fprintf(stderr," Matches:{");
	of12_dump_matches(entry->matchs);
	fprintf(stderr,"}\n\t\t");
	of12_dump_instructions(entry->inst_grp);
	fprintf(stderr,"\n");
}

/**
 * check if the entry is valid for insertion
 */
rofl_result_t of12_validate_flow_entry(of12_group_table_t *gt, of12_flow_entry_t* entry){
	//TODO
	int i, j;
	of12_packet_action_t *pa_it;
	of12_action_group_t *ac_it;
	
	//if there is a group action we should check that the group exists
	for(i=0;i<OF12_IT_GOTO_TABLE;i++){
		switch(entry->inst_grp.instructions[i].type){
			case OF12_IT_NO_INSTRUCTION:
				continue;
				break;
				
			case OF12_IT_APPLY_ACTIONS:
				of12_validate_action_group(entry->inst_grp.instructions[i].apply_actions);
				ac_it = entry->inst_grp.instructions[i].apply_actions;
				if(ac_it){
					for(pa_it=ac_it->head; pa_it; pa_it=pa_it->next){
						if(pa_it->type == OF12_AT_GROUP && (pa_it->group=of12_group_search(gt,pa_it->field))==NULL){
								return ROFL_FAILURE;
						}
					}
				}
				break;
				
			case OF12_IT_WRITE_ACTIONS:
				of12_validate_write_actions(entry->inst_grp.instructions[i].write_actions);
				for(j=0;j<OF12_AT_NUMBER;j++){
					pa_it = &(entry->inst_grp.instructions[i].write_actions->write_actions[j]);
					if(pa_it && pa_it->type == OF12_AT_GROUP && (pa_it->group=of12_group_search(gt,pa_it->field))==NULL ){
						return ROFL_FAILURE;
					}
				}
				break;
				
			default:
				continue;
				break;
		}
	}
	
	return ROFL_SUCCESS;
}
