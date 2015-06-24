#include "trie.h"
#include "rofl/datapath/pipeline/openflow/openflow1x/pipeline/matching_algorithms/trie/of1x_trie_ma.h"


static of1x_switch_t* sw = NULL;
static of1x_trie_t* trie = NULL;
static of1x_flow_table_t* table = NULL;

int set_up(){

	physical_switch_init();

	enum of1x_matching_algorithm_available ma_list[4]={of1x_trie_matching_algorithm, of1x_trie_matching_algorithm,
	of1x_trie_matching_algorithm, of1x_trie_matching_algorithm};

	//Create instance
	sw = of1x_init_switch("Test switch", OF_VERSION_12, 0x0101,4,ma_list);
	table = &sw->pipeline.tables[0];
	trie = (of1x_trie_t*)table->matching_aux[0];

	if(!sw)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

int tear_down(){
	//Destroy the switch
	if(__of1x_destroy_switch(sw) != ROFL_SUCCESS)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

void test_install_empty_flowmods(){

	of1x_flow_entry_t *entry, *tmp;

	entry = of1x_init_flow_entry(false);
	CU_ASSERT(entry != NULL);
	entry->priority=100;
	//Force entry to have invalid values in next and prev
	entry->next = entry->prev = (void*)0x1;

	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	CU_ASSERT(entry == NULL);

	CU_ASSERT(table->num_of_entries == 1);
	CU_ASSERT(trie->entry != NULL);
	if(trie->entry){
		CU_ASSERT(trie->entry->next == NULL);
		CU_ASSERT(trie->entry->prev == NULL);
		CU_ASSERT(trie->entry->priority == 100);
	}

	//Add a second entry with lower priority
	entry = of1x_init_flow_entry(false);
	CU_ASSERT(entry != NULL);
	entry->priority=100;
	entry->next = entry->prev = (void*)0x1;

	//Overlap must fail
	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, true,false) == ROFL_OF1X_FM_OVERLAP);
	entry->priority=99;

	//Add without overlap
	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	CU_ASSERT(entry == NULL);
	CU_ASSERT(table->num_of_entries == 2);
	CU_ASSERT(trie->entry != NULL);
	if(trie->entry){
		CU_ASSERT(trie->entry->prev == NULL);
		CU_ASSERT(trie->entry->priority == 100);
		CU_ASSERT(trie->entry->next != NULL);
		CU_ASSERT(trie->entry->next->prev == trie->entry);
		CU_ASSERT(trie->entry->next->next == NULL);
		CU_ASSERT(trie->entry->next->priority == 99);
	}

	//Add one with higher priority
	entry = of1x_init_flow_entry(false);
	CU_ASSERT(entry != NULL);
	entry->priority=110;
	entry->next = entry->prev = (void*)0x1;

	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	CU_ASSERT(entry == NULL);
	CU_ASSERT(table->num_of_entries == 3);
	CU_ASSERT(trie->entry != NULL);
	if(trie->entry){
		CU_ASSERT(trie->entry->prev == NULL);
		CU_ASSERT(trie->entry->priority == 110);
		CU_ASSERT(trie->entry->next != NULL);
		CU_ASSERT(trie->entry->next->priority == 100);
		CU_ASSERT(trie->entry->next->prev == trie->entry);
		CU_ASSERT(trie->entry->next->next != NULL);
		CU_ASSERT(trie->entry->next->next->priority == 99);
		CU_ASSERT(trie->entry->next->next->prev == trie->entry->next);
		CU_ASSERT(trie->entry->next->next->next == NULL);
	}

	//Add one in between
	entry = of1x_init_flow_entry(false);
	CU_ASSERT(entry != NULL);
	entry->priority=100;
	entry->next = entry->prev = (void*)0x1;

	//Overlap must fail
	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, true,false) == ROFL_OF1X_FM_OVERLAP);
	entry->priority=107;

	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	CU_ASSERT(entry == NULL);
	CU_ASSERT(table->num_of_entries == 4);
	CU_ASSERT(trie->entry != NULL);
	if(trie->entry){
		CU_ASSERT(trie->entry->prev == NULL);
		CU_ASSERT(trie->entry->priority == 110);
		CU_ASSERT(trie->entry->next != NULL);
		CU_ASSERT(trie->entry->next->priority == 107);
		CU_ASSERT(trie->entry->next->prev == trie->entry);
		CU_ASSERT(trie->entry->next->next != NULL);
		CU_ASSERT(trie->entry->next->next->priority == 100);
		CU_ASSERT(trie->entry->next->next->prev == trie->entry->next);
		CU_ASSERT(trie->entry->next->next->next != NULL);
		CU_ASSERT(trie->entry->next->next->next->prev == trie->entry->next->next);
		CU_ASSERT(trie->entry->next->next->next->priority == 99);
		CU_ASSERT(trie->entry->next->next->next->next == NULL);
	}

	//Override priority == 99
	entry = of1x_init_flow_entry(false);
	tmp = entry; //hold pointer
	CU_ASSERT(entry != NULL);
	entry->priority=99;
	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	CU_ASSERT(entry == NULL);
	CU_ASSERT(table->num_of_entries == 4);
	CU_ASSERT(trie->entry != NULL);
	if(trie->entry){
		CU_ASSERT(trie->entry->next->next->next->priority == 99);
		CU_ASSERT(trie->entry->next->next->next == tmp);
	}

}

void test_install_flowmods_nomask(){

	of1x_flow_entry_t *entry, *tmp;

	//Create a flowmod with a single match
	entry = of1x_init_flow_entry(false);
	entry->priority=999;
	CU_ASSERT(entry != NULL);

	//192.168.0.1
	CU_ASSERT(of1x_add_match_to_entry(entry,of1x_init_ip4_src_match(0xC0A80001, 0xFFFFFFFF)) == ROFL_SUCCESS);

	tmp = entry;
	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	CU_ASSERT(entry == NULL);
	CU_ASSERT(table->num_of_entries == 5);
	CU_ASSERT(trie->root != NULL);
	CU_ASSERT(trie->root->parent == NULL);
	CU_ASSERT(trie->root->inner == NULL);
	CU_ASSERT(trie->root->entry == tmp);
	CU_ASSERT(trie->root->entry->next == NULL);
	CU_ASSERT(trie->root->next == NULL);
	CU_ASSERT(trie->root->match.__tern.value.u32 == HTONB32(0xC0A80001));
	CU_ASSERT(trie->root->match.__tern.mask.u32 == HTONB32(0xFFFFFFFF));
	CU_ASSERT(trie->root->inner_max_priority = 999);

	//Add a flowmod that shares most of it (except two bits)
	//Creates an intermediate leaf
	entry = of1x_init_flow_entry(false);
	CU_ASSERT(entry != NULL);

	//192.168.0.2
	CU_ASSERT(of1x_add_match_to_entry(entry,of1x_init_ip4_src_match(0xC0A80002, 0xFFFFFFFF)) == ROFL_SUCCESS);
	entry->priority=1999;

	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	CU_ASSERT(entry == NULL);
	CU_ASSERT(table->num_of_entries == 6);
	CU_ASSERT(trie->root != NULL);
	CU_ASSERT(trie->root->parent == NULL);
	CU_ASSERT(trie->root->inner != NULL);
	CU_ASSERT(trie->root->entry == NULL);
	CU_ASSERT(trie->root->next == NULL);
	CU_ASSERT(trie->root->match.__tern.value.u32 == HTONB32(0xC0A80000));
	CU_ASSERT(trie->root->match.__tern.mask.u32 == HTONB32(0xFFFFFFFC));
	CU_ASSERT(trie->root->inner_max_priority = 1999);
	CU_ASSERT(trie->root->inner->entry != NULL);
	CU_ASSERT(trie->root->inner->next->entry != NULL);
	CU_ASSERT(trie->root->inner->next->next == NULL);

	//Try overlapping
	entry = of1x_init_flow_entry(false);
	CU_ASSERT(entry != NULL);

	//192.168.0.0/24
	CU_ASSERT(of1x_add_match_to_entry(entry,of1x_init_ip4_src_match(0xC0A80002, 0xFFFFFF00)) == ROFL_SUCCESS);
	entry->priority=1999;
	CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, true,false) == ROFL_OF1X_FM_OVERLAP);
	entry->priority=1999;
	//CU_ASSERT(of1x_add_flow_entry_table(&sw->pipeline, 0, &entry, false,false) == ROFL_OF1X_FM_SUCCESS);
	//CU_ASSERT(table->num_of_entries == 7);


}

void test_multiple_masks(){
}
