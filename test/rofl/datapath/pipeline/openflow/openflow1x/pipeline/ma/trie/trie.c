#include "trie.h"

static of1x_switch_t* sw=NULL;

int set_up(){

	physical_switch_init();

	enum of1x_matching_algorithm_available ma_list[4]={of1x_trie_matching_algorithm, of1x_trie_matching_algorithm,
	of1x_trie_matching_algorithm, of1x_trie_matching_algorithm};

	//Create instance
	sw = of1x_init_switch("Test switch", OF_VERSION_12, 0x0101,4,ma_list);

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

void test_install_invalid_flowmods(){
}

void test_install_overlapping_specific(){
}

void test_multiple_masks(){
}
