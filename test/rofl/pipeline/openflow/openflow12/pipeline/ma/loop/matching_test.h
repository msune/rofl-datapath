#ifndef MATCHING_TEST
#define MATCHING_TEST

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <CUnit/Basic.h>

#include "rofl/pipeline/openflow/openflow12/of12_switch.h"
#include "rofl/pipeline/openflow/openflow12/pipeline/of12_flow_entry.h"
#include "rofl/pipeline/openflow/openflow12/pipeline/of12_flow_table.h"
#include "rofl/pipeline/openflow/openflow12/pipeline/matching_algorithms/matching_algorithms_available.h"
#include "rofl/pipeline/openflow/openflow12/pipeline/matching_algorithms/loop/of12_loop_match.h"

/* Setup/teardown */
int set_up(void);
int tear_down(void);
	
/* Test cases */
void test_install_simple_flow_mod(void);
void test_uninstall_all(void);
void test_overlap(void);


#endif