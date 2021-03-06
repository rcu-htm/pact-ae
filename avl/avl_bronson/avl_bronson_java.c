/*   
 *   File: bst_bronson_java.c
 *   Author: Balmau Oana <oana.balmau@epfl.ch>, 
 *  	     Zablotchi Igor <igor.zablotchi@epfl.ch>, 
 *  	     Tudor David <tudor.david@epfl.ch>
 *   Description: Nathan G. Bronson, Jared Casper, Hassan Chafi
 *   , and Kunle Olukotun. A Practical Concurrent Binary Search Tree. 
 *   PPoPP 2010.
 *   bst_bronson_java.c is part of ASCYLIB
 *
 * Copyright (c) 2014 Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>,
 * 	     	      Tudor David <tudor.david@epfl.ch>
 *	      	      Distributed Programming Lab (LPD), EPFL
 *
 * ASCYLIB is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "avl_bronson_java.h"
#include <stdlib.h>
#include <pthread.h>

//RETRY_STATS_VARS;

//__thread ssmem_allocator_t* alloc;

volatile node_t* bst_initialize() {

  volatile node_t* root = new_node(0, 0, 0, 0, NULL, NULL, NULL, TRUE);

  return root;
}

 volatile node_t* new_node(int height, skey_t key, uint64_t version, sval_t value, volatile node_t* parent, volatile node_t* left, volatile node_t* right, bool_t initializing) {

    volatile node_t* node;

//#if GC == 1
//    if (unlikely(initializing))  {
//        node = (volatile node_t *) ssalloc_aligned(CACHE_LINE_SIZE, sizeof(node_t));
//    } else {
//        node = (volatile node_t *) ssmem_alloc(alloc, sizeof(node_t));
//    }
//#else
//    node = (volatile node_t *) ssalloc_aligned(CACHE_LINE_SIZE, sizeof(node_t));
//#endif

	node = malloc(sizeof(node_t));
    if (node == NULL) {
        perror("malloc in bst create node");
        exit(1);
    }

    node->height = height;
    node->key = key;
    node->version = version;
    node->value = value;
    node->parent = parent;
    node->left = left;
    node->right = right;
    INIT_LOCK(&(node->lock));

//    asm volatile("" ::: "memory");
//#ifdef __tile__
//    MEM_BARRIER;
//#endif
	__sync_synchronize(); /* JIMSIAK */
    return node;
}

// When the function returns 0, it means that the node was not found
// (similarly to Howley)
sval_t bst_contains(skey_t key, volatile node_t* root) {
	// printf("Bst contains (key %d)\n", key);
    while(TRUE) {
//      PARSE_TRY();
		volatile node_t* right = root->right;

		if (right == NULL) {
            // printf("ret1: right==NuLL\n");
			return FALSE;
		} else {
			volatile int right_cmp = key - right->key;

			if (right_cmp == 0) {
                // printf("ret2: right_cmp == 0\n");
				return right->value;
			}

			volatile uint64_t ovl = right->version;
            //if ((ovl & 3)) {
            if(IS_SHRINKING_OR_UNLINKED(ovl)){
                wait_until_not_changing(right);
            } else if(right == root->right){
            	// if right_cmp < 0, we should go left, otherwise right
                sval_t vo = attempt_get(key, right, (right_cmp < 0 ? FALSE : TRUE), ovl);
                //CHANGE
                if (vo != RETRY) {
                    //return vo == FOUND;
                    if (vo != NOT_FOUND) {
                         //printf("ret3: vo != NOT_FOUND, vo = %d\n", vo);
                        return vo;
                    } else { 
                        // printf("ret4: vo != NOT_FOUND, vo = %d\n", vo);
                        return 0;
                    }
                }
            }
        }
    }
}

sval_t attempt_get(skey_t k, volatile node_t* node, bool_t is_right, uint64_t node_v) {

    // printf("Attempt get: skey %d\n", k);
	while(TRUE){
        volatile node_t* child = CHILD(node, is_right);

        if(child == NULL){
            if(node->version != node_v){
                // printf("ret5: node->version != node_v\n");

                return RETRY;
            }

            return NOT_FOUND;
        } else {
            int child_cmp = k - child->key;

            if(child_cmp == 0){
            	//Verify that it's a value node
                //CHANGE
                //TODO: Leave NOT_FOUND or change to 0?
                // printf("ret6: child_cmp == 0, child->value: %d\n", child->value);

                return child->value ? child->value : NOT_FOUND;
            }

            uint64_t child_ovl = child->version;
            //if ((child_ovl & 3)){
            if(IS_SHRINKING_OR_UNLINKED(child_ovl)){
                wait_until_not_changing(child);

                if(node->version != node_v){
                    // printf("ret7: node->version != node_v\n");

                    return RETRY;
                }
            } else if(child != CHILD(node, is_right)){
                if(node->version != node_v){
                    // printf("ret8: node->version != node_v\n");

                    return RETRY;
                }
            } else {
                if(node->version != node_v){
                 // printf("ret9: node->version != node_v\n");
  
                  return RETRY;
                }

                sval_t result = attempt_get(k, child, (child_cmp < 0 ? FALSE : TRUE), child_ovl);
                if(result != RETRY){
                    //CHANGE (leave like this)
                    // printf("ret10: result %d\n", result);

                    return result;
                }
            }
        }
    }
}

bool_t bst_add(skey_t key, sval_t v, volatile node_t* root) {
	//If something is already present at that particular key, the new value will not be added.
    sval_t res = update_under_root(key, UPDATE_IF_ABSENT, v, root);
    return res == NOT_FOUND || res == 0;
}

sval_t bst_remove(skey_t key, volatile node_t* root) {
    sval_t res =  update_under_root(key, UPDATE_IF_PRESENT, 0, root);
    return res == NOT_FOUND ? 0 : res;
}

sval_t update_under_root(skey_t key, function_t func, sval_t new_value, volatile node_t* holder) {

	while(TRUE){
//	  PARSE_TRY();
//	  UPDATE_TRY();

        volatile node_t* right = holder->right;

        if(right == NULL){
            if(!SHOULD_UPDATE(func, 0)){
                return NO_UPDATE_RESULT(func, 0);
            }

            if(new_value == 0 || attempt_insert_into_empty(key, new_value, holder)){
                return UPDATE_RESULT(func, 0);
            }
        } else {
            uint64_t ovl = right->version;

            //if ((ovl & 3)){
            if(IS_SHRINKING_OR_UNLINKED(ovl)){
                wait_until_not_changing(right);
            } else if(right == holder->right){
                sval_t vo = attempt_update(key, func, new_value, holder, right, ovl);
                if(vo != RETRY){
                    // printf("Return from update_under root, vo %d\n", vo);
                    return vo == NOT_FOUND ? 0 : vo;   
                }
            }
        }
    }
}

bool_t attempt_insert_into_empty(skey_t key, sval_t value, volatile node_t* holder){

    skey_t UNUSED holder_key = holder->key;

    //printf("Lock node: %d\n", holder_key);
    volatile ptlock_t* holder_lock = &holder->lock;
    LOCK(holder_lock);


    if(holder->right == NULL){
        holder->right = new_node(1, key, 0, value, holder, NULL, NULL, FALSE);
        holder->height = 2;

        UNLOCK(holder_lock);
        return TRUE;
    } else {

    	UNLOCK(holder_lock);
        return FALSE;
    }
}

sval_t attempt_update(skey_t key, function_t func, sval_t new_value, volatile node_t* parent, volatile node_t* node, uint64_t node_v) {

    // printf("attempt_update: key %d, new_value %d\n", key, new_value);
	int cmp = key - node->key;
   
    if(cmp == 0){
        sval_t res = attempt_node_update(func, new_value, parent, node);
        return res;
    }

    bool_t is_right = cmp < 0 ? FALSE : TRUE ;
    
    while(TRUE){

        volatile node_t* child = CHILD(node, is_right);

        if(node->version != node_v){
            // printf("Retrying 3\n");

            return RETRY;
        }

        if(child == NULL){

            if(new_value == 0){
                
                return NOT_FOUND;
            } else {
                bool_t success;
                volatile node_t* damaged;

                {
                    // publish(node);
                    skey_t UNUSED node_key = node->key;
                    volatile ptlock_t* node_lock = &node->lock;
                    LOCK(node_lock); 
                
                    if(node->version != node_v){
                        // releaseAll();
                        UNLOCK(node_lock);
                        //      f("Retrying 2\n");
                        return RETRY;
                    }

                    if(CHILD(node, is_right) != NULL){
                        success = FALSE;
                        damaged = NULL;
                    } else {
                        if(!SHOULD_UPDATE(func, 0)){
                            // releaseAll();
                            UNLOCK(node_lock);
                            
                            return NO_UPDATE_RESULT(func, 0);
                        }

                        volatile node_t* new_child = new_node(1, key, 0, new_value, node, NULL, NULL, FALSE);
                        set_child(node, new_child, is_right);

                        success = TRUE;
                        damaged = fix_height_nl(node);
                    }
                    
                    // releaseAll();
                    UNLOCK(node_lock);
                }

                if(success){
                    fix_height_and_rebalance(damaged);
                    
                    return UPDATE_RESULT(func, 0);
                }
            }

        } else {
            uint64_t child_v = child->version;

            //if ((child_v & 3)){
            if(IS_SHRINKING_OR_UNLINKED(child_v)){
                wait_until_not_changing(child);
            } else if(child != CHILD(node, is_right)){
                //RETRY
            } else {
                if(node->version != node_v){
                    // printf("Retrying 1\n");
                    return RETRY;
                }

                sval_t vo = attempt_update(key, func, new_value, node, child, child_v);
                // if (vo ==RETRY) printf("Retrying - vo %d\n", vo);
                if(vo != RETRY){
                    return vo == NOT_FOUND ? 0 : vo;   
                }
            }
        }
    }
}

sval_t attempt_node_update(function_t func, sval_t new_value, volatile node_t* parent, volatile node_t* node) {


	if(new_value == 0){
        if(node->value == 0){
            
            return NOT_FOUND;
        }
    }

    if(new_value == 0 && (node->left == NULL || node->right == NULL)){
        
        sval_t prev;
        volatile node_t* damaged;

        {
            // publish(parent);
            // scoped_lock parentLock(parent->lock);
            skey_t UNUSED parent_key = parent->key;
            volatile ptlock_t* parent_lock = &parent->lock;
            LOCK(parent_lock);
            
            if(IS_UNLINKED(parent->version) || node->parent != parent){
                // releaseAll();
                UNLOCK(parent_lock);
                return RETRY;
            }

            {
                // publish(node);
                // scoped_lock lock(node->lock);
                skey_t UNUSED node_key = node->key;
                volatile ptlock_t* node_lock = &node->lock;
                LOCK(node_lock);
                
                prev = node->value;

                if(!SHOULD_UPDATE(func, prev)){
                    // releaseAll();
                    UNLOCK(node_lock);
                    UNLOCK(parent_lock);
                    return NO_UPDATE_RESULT(func, prev);
                }

                if(prev == 0){
                    // releaseAll();
                    UNLOCK(node_lock);
                    UNLOCK(parent_lock);
                    return UPDATE_RESULT(func, prev);
                }

                if(!attempt_unlink_nl(parent, node)){
                    // releaseAll();
                    UNLOCK(node_lock);
                    UNLOCK(parent_lock);
                    return RETRY;
                }

                UNLOCK(node_lock);
            }
            
            // releaseAll();

            damaged = fix_height_nl(parent);
            UNLOCK(parent_lock);
        }

        fix_height_and_rebalance(damaged);
        
        return UPDATE_RESULT(func, prev);
    } else {
        // publish(node);
        // scoped_lock lock(node->lock);
        skey_t UNUSED node_key = node->key;
        volatile ptlock_t* node_lock = &node->lock;
        LOCK(node_lock);

        if(IS_UNLINKED(node->version)){
            // releaseAll();
            UNLOCK(node_lock);
            return RETRY;
        }

        sval_t prev = node->value;
        if(!SHOULD_UPDATE(func, prev)){
			// releaseAll();
			UNLOCK(node_lock);
            return NO_UPDATE_RESULT(func, prev);
        }

        if(new_value == 0 && (node->left == NULL || node->right == NULL)){
            // releaseAll();
            UNLOCK(node_lock);
            return RETRY;
        }

        node->value = new_value;
        
        // releaseAll();
        UNLOCK(node_lock);
        return UPDATE_RESULT(func, prev);
    }
}

void wait_until_not_changing(volatile node_t* node) {
//  CLEANUP_TRY();

	volatile uint64_t version = node->version;	
    int i;

    //if ((version & 1)) {
    if (IS_SHRINKING(version)) {

      for (i = 0; i < SPIN_COUNT; ++i) {
	if (version != node->version) {
	  return;
	}
      }

	  __sync_synchronize(); /* JIMSIAK */
//      MEM_BARRIER;
      /* skey_t UNUSED node_key = node->key; */
      /* volatile ptlock_t* node_lock = &node->lock; */
      /* LOCK(node_lock); */
      /* UNLOCK(node_lock); */
    }
}

bool_t attempt_unlink_nl(volatile node_t* parent, volatile node_t* node) {

	volatile node_t* parent_l = parent->left;
    volatile node_t* parent_r = parent->right;

    if(parent_l != node && parent_r != node){
        return FALSE;
    }

    volatile node_t* left = node->left;
    volatile node_t* right = node->right;

    if(left != NULL && right != NULL){

        return FALSE;
    }

    volatile node_t* splice = (left != NULL) ? left : right;
    
    if(parent_l == node){
        parent->left = splice;
    } else {
        parent->right = splice;
    }

    if(splice != NULL){
        splice->parent = parent;
    }

    node->version = UNLINKED_OVL;
    node->value = 0;

#if GC==1
    ssmem_free(alloc, (void*) node);
#endif
    // hazard.releaseNode(node);
    return TRUE;
}

int node_conditon(volatile node_t* node) {

	volatile node_t* nl = node->left;
    volatile node_t* nr = node->right;

    if((nl == NULL || nr == NULL) && node->value == 0){
        
        return UNLINK_REQUIRED;
    }

    int hn = node->height;
    int hl0 = HEIGHT(nl);
    int hr0 = HEIGHT(nr);
    int hnrepl = 1 + max(hl0, hr0);
    int bal = hl0 - hr0;

    if(bal < -1 || bal > 1){

        return REBALANCE_REQUIRED;
    }

    return hn != hnrepl ? hnrepl : NOTHING_REQUIRED;
}

volatile node_t* fix_height_nl(volatile node_t* node){

    int c = node_conditon(node);

    switch(c){
        case REBALANCE_REQUIRED:
        case UNLINK_REQUIRED:
            return node;
        case NOTHING_REQUIRED:
            return NULL;
        default:
            node->height = c;
            return node->parent;
    }
}

/*** Beginning of rebalancing functions ***/

void fix_height_and_rebalance(volatile node_t* node) {
    
    while(node != NULL && node->parent != NULL){
        
        
        int condition = node_conditon(node);
        if(condition == NOTHING_REQUIRED || IS_UNLINKED(node->version)){
            return;
        }

        if(condition != UNLINK_REQUIRED && condition != REBALANCE_REQUIRED){
            // publish(node);
            // scoped_lock lock(node->lock);

            skey_t UNUSED node_key = node->key;

            volatile ptlock_t* node_lock = &node->lock;
            LOCK(node_lock);
            
            node = fix_height_nl(node);

            UNLOCK(node_lock);

            // releaseAll();
        } else {

            volatile node_t* n_parent = node->parent;
            // publish(n_parent);
            // scoped_lock lock(n_parent->lock);
            //skey_t UNUSED n_parent_key = n_parent->key;
            volatile ptlock_t* n_parent_lock = &n_parent->lock;
            LOCK(n_parent_lock);

            if(!IS_UNLINKED(n_parent->version) && node->parent == n_parent){
                // publish(node);
                // scoped_lock nodeLock(node->lock);
                skey_t UNUSED node_key = node->key;
                volatile ptlock_t* node_lock = &node->lock;
                LOCK(node_lock);

                node = rebalance_nl(n_parent, node);

                UNLOCK(node_lock);
            }

            UNLOCK(n_parent_lock);
            // releaseAll();
        }
    }    
}

volatile node_t* rebalance_nl(volatile node_t* n_parent, volatile node_t* n){

	volatile node_t* nl = n->left;
    volatile node_t* nr = n->right;

    if((nl == NULL || nr == NULL) && n->value == 0){
        if(attempt_unlink_nl(n_parent, n)){
            return fix_height_nl(n_parent);
        } else {
            return n;
        }
    }
    
    int hn = n->height;
    int hl0 = HEIGHT(nl);
    int hr0 = HEIGHT(nr);
    int hnrepl = 1 + max(hl0, hr0);
    int bal = hl0 - hr0;

    if(bal > 1){
        return rebalance_to_right_nl(n_parent, n, nl, hr0);
    } else if(bal < -1){
        return rebalance_to_left_nl(n_parent, n, nr, hl0);
    } else if(hnrepl != hn) {
        n->height = hnrepl;

        return fix_height_nl(n_parent);
    } else {
        return NULL;
    }
}

// checked
volatile node_t* rebalance_to_right_nl(volatile node_t* n_parent, volatile node_t* n, volatile node_t* nl, int hr0) {
    
    skey_t UNUSED nl_key = nl->key;
    volatile ptlock_t* nl_lock = &nl->lock;
	LOCK(nl_lock);

	int hl = nl->height;
    if(hl - hr0 <= 1){
    	UNLOCK(nl_lock);
        return n;
    } else {
        // publish(nl->right);
        volatile node_t* nlr = nl->right;

        int hll0 = HEIGHT(nl->left);
        int hlr0 = HEIGHT(nlr);


        if(hll0 >= hlr0){ 
        	volatile node_t* res = rotate_right_nl(n_parent, n, nl, hr0, hll0, nlr, hlr0);
            UNLOCK(nl_lock);
            return res ;
        } else {
            {

                // scoped_lock sublock(nlr->lock);
                skey_t UNUSED nlr_key = nlr->key;
                volatile ptlock_t* nlr_lock = &nlr->lock;
                LOCK(nlr_lock);

                int hlr = nlr->height;
                if(hll0 >= hlr){
                	volatile node_t* res = rotate_right_nl(n_parent, n, nl, hr0, hll0, nlr, hlr);
                	
                    UNLOCK(nlr_lock);
                    UNLOCK(nl_lock);
                    return res;
                } else {
                    int hlrl = HEIGHT(nlr->left);
                    int b = hll0 - hlrl;

                    // CHANGED: Java and C++ implementations differ
                    if(b >= -1 && b <= 1 && !((hll0 == 0 || hlrl == 0) && nl->value == 0)){
                    	volatile node_t* res = rotate_right_over_left_nl(n_parent, n, nl, hr0, hll0, nlr, hlrl);
                        UNLOCK(nlr_lock);
                        UNLOCK(nl_lock);
                        return res;
                    }
                }

                // CHANGED
                UNLOCK(nlr_lock);
            }

            volatile node_t* res = rebalance_to_left_nl(n, nl, nlr, hll0);
            UNLOCK(nl_lock);
            return res;
        }
    }

}

volatile node_t* rebalance_to_left_nl(volatile node_t* n_parent, volatile node_t* n, volatile node_t* nr, int hl0) {


	// publish(nR);
    // scoped_lock lock(nR->lock);
    
    skey_t UNUSED nr_key = nr->key;
    volatile ptlock_t* nr_lock = &nr->lock;
	LOCK(nr_lock);

    int hr = nr->height;
    if(hl0 - hr >= -1){
    	UNLOCK(nr_lock);
        return n;
    } else {
        volatile node_t* nrl = nr->left;
        int hrl0 = HEIGHT(nrl);
        int hrr0 = HEIGHT(nr->right);

        if(hrr0 >= hrl0){

            volatile node_t* res = rotate_left_nl(n_parent, n, hl0, nr, nrl, hrl0, hrr0);
            UNLOCK(nr_lock);
            return res;
        } else {
            {
                // publish(nrl);
                // scoped_lock sublock(nrl->lock);
	        skey_t UNUSED nrl_key = nrl->key;
                volatile ptlock_t* nrl_lock = &nrl->lock;
                LOCK(nrl_lock);

                int hrl = nrl->height;
                if(hrr0 >= hrl){
                	volatile node_t* res = rotate_left_nl(n_parent, n, hl0, nr, nrl, hrl, hrr0);
                    UNLOCK(nrl_lock);
                	UNLOCK(nr_lock);
                    return res;
                } else {
                    int hrlr = HEIGHT(nrl->right);
                    int b = hrr0 - hrlr;
                    // CHANGED
                    if(b >= -1 && b <= 1 && !((hrr0 == 0 || hrlr == 0) && nr->value == 0)){
                    	volatile node_t* res = rotate_left_over_right_nl(n_parent, n, hl0, nr, nrl, hrr0, hrlr);

                        UNLOCK(nrl_lock);
                		UNLOCK(nr_lock);
                        return res;
                    }
                }

                UNLOCK(nrl_lock);

            }
            volatile node_t* res = rebalance_to_right_nl(n, nr, nrl, hrr0);
            UNLOCK(nr_lock);
            return res;
        }
    }

}

volatile node_t* rotate_right_nl(volatile node_t* n_parent, volatile node_t* n, volatile node_t* nl, int hr, int hll, volatile node_t* nlr, int hlr) {

	uint64_t node_ovl = n->version;
    volatile node_t* npl = n_parent->left;
    n->version = BEGIN_CHANGE(node_ovl);

    n->left = nlr;
    if(nlr != NULL){
        nlr->parent = n;
    }

    nl->right = n;
    n->parent = nl;

    if(npl == n){
        n_parent->left = nl;
    } else {
        n_parent->right = nl;
    }
    nl->parent = n_parent;

    int hnrepl = 1 + max(hlr, hr);
    n->height = hnrepl;
    nl->height = 1 + max(hll, hnrepl);

    n->version = END_CHANGE(node_ovl);

    int baln = hlr - hr;
    if(baln < -1 || baln > 1){
        return n;
    }

    // CHANGED 
    if ((nlr == NULL || hr == 0) && n->value == 0) {
            return n;
    }

    int ball = hll - hnrepl;
    if(ball < -1 || ball > 1){
        return nl;
    }

    // CHANGED
    if (hll == 0 && nl->value == 0) {
            return nl;
    }

    return fix_height_nl(n_parent);
}

volatile node_t* rotate_left_nl(volatile node_t* n_parent, volatile node_t* n, int hl, volatile node_t* nr, volatile node_t* nrl, int hrl, int hrr){

    uint64_t node_ovl = n->version;
    volatile node_t* npl = n_parent->left;
    n->version = BEGIN_CHANGE(node_ovl);

    n->right = nrl;
    if(nrl != NULL){
        nrl->parent = n;
    }

    nr->left = n;
    n->parent = nr;

    if(npl == n){
        n_parent->left = nr;
    } else {
        n_parent->right = nr;
    }
    nr->parent = n_parent;

    int hnrepl = 1 + max(hl, hrl);
    n->height = hnrepl;
    nr->height = 1 + max(hnrepl, hrr);

    n->version = END_CHANGE(node_ovl);

    int baln = hrl - hl;
    if(baln < -1 || baln > 1){
        return n;
    }

    // CHANGED
    if ((nrl == NULL || hl == 0) && n->value == 0) {
            return n;
    }

    int balr = hrr - hnrepl;
    if(balr < -1 || balr > 1){
        return nr;
    }

    // CHANGED
    if (hrr == 0 && nr->value == 0) {
        return nr;
    }


    return fix_height_nl(n_parent);
}

volatile node_t* rotate_right_over_left_nl(volatile node_t* n_parent, volatile node_t* n, volatile node_t* nl, int hr, int hll, volatile node_t* nlr, int hlrl){

    uint64_t node_ovl = n->version;
    uint64_t left_ovl = nl->version;

    volatile node_t* npl = n_parent->left;
    volatile node_t* nlrl = nlr->left;
    volatile node_t* nlrr = nlr->right;
    int hlrr = HEIGHT(nlrr);

    n->version = BEGIN_CHANGE(node_ovl);
    nl->version = BEGIN_CHANGE(left_ovl);

    n->left = nlrr;
    if(nlrr != NULL){
        nlrr->parent = n;
    }

    nl->right = nlrl;
    if(nlrl != NULL){
        nlrl->parent = nl;
    }

    nlr->left = nl;
    nl->parent = nlr;
    nlr->right = n;
    n->parent = nlr;

    if(npl == n){
        n_parent->left = nlr;
    } else {
        n_parent->right = nlr;
    }
    nlr->parent = n_parent;

    int hnrepl = 1 + max(hlrr, hr);
    n->height = hnrepl;

    int hlrepl = 1 + max(hll, hlrl);
    nl->height = hlrepl;

    nlr->height = 1 + max(hlrepl, hnrepl);

    n->version = END_CHANGE(node_ovl);
    nl->version = END_CHANGE(left_ovl);

    int baln = hlrr - hr;
    if(baln < -1 || baln > 1){
        return n;
    }

    if ((nlrr == NULL || hr == 0) && n->value == 0) {
        // repair involves splicing out n and maybe more rotations
        return n;
    }

    int ballr = hlrepl - hnrepl;
    if(ballr < -1 || ballr > 1){
        return nlr;
    }
    
    return fix_height_nl(n_parent);
}

volatile node_t* rotate_left_over_right_nl(volatile node_t* n_parent, volatile node_t* n, int hl, volatile node_t* nr, volatile node_t* nrl, int hrr, int hrlr){

    uint64_t node_ovl = n->version;
    uint64_t right_ovl = nr->version;

    // CHANGED
    n->version = BEGIN_CHANGE(node_ovl);
    nr->version = BEGIN_CHANGE(right_ovl);
    
    volatile node_t* npl = n_parent->left;
    volatile node_t* nrll = nrl->left;
    volatile node_t* nrlr = nrl->right;
    int hrll = HEIGHT(nrll);


    n->right = nrll;
    if(nrll != NULL){
        nrll->parent = n;
    }

    nr->left = nrlr;
    if(nrlr != NULL){
        nrlr->parent = nr;
    }

    nrl->right = nr;
    nr->parent = nrl;
    nrl->left = n;
    n->parent = nrl;

    if(npl == n){
        n_parent->left = nrl;
    } else {
        n_parent->right = nrl;
    }
    nrl->parent = n_parent;

    int hnrepl = 1 + max(hl, hrll);
    n->height = hnrepl;
    int hrrepl = 1 + max(hrlr, hrr);
    nr->height = hrrepl;
    nrl->height = 1 + max(hnrepl, hrrepl);

    n->version = END_CHANGE(node_ovl);
    nr->version = END_CHANGE(right_ovl);

    int baln = hrll - hl;
    if(baln < -1 || baln > 1){
        return n;
    }

    // CHANGED
    if ((nrll == NULL || hl == 0) && n->value == 0) {
        return n;
    }

    int balrl = hrrepl - hnrepl;
    if(balrl < -1 || balrl > 1){
        return nrl;
    }
    
    return fix_height_nl(n_parent);
}

/*** End of rebalancing functions ***/

void set_child(volatile node_t* parent, volatile node_t* child, bool_t is_right) {
	if (is_right) {
		parent->right = child;
	} else {
		parent->left = child;
	}
}

uint64_t bst_size(volatile node_t* node) {
	if (node == NULL || node->version == UNLINKED_OVL) {
		return 0;
	} else if (node->value == 0) {
		return bst_size(node->left) + bst_size(node->right);
	} else {
		return 1 + bst_size(node->left) + bst_size(node->right);
	}
}

static void avl_print_rec(volatile node_t *root, int level)
{
	int i;

	if (root)
		avl_print_rec(root->right, level + 1);

	for (i = 0; i < level; i++)
		printf("|--");

	if (!root) {
		printf("NULL\n");
		return;
	}

	printf("%d[%d](%p)\n", root->key, root->height, root);

	avl_print_rec(root->left, level + 1);
}

static void avl_print_struct(volatile node_t *root)
{
	if (root == NULL)
		printf("[empty]");
	else
		avl_print_rec(root, 0);
	printf("\n");
}



static inline int _avl_warmup_helper(node_t *root, int nr_nodes, int max_key,
                                     unsigned int seed, int force)
{
	int i, nodes_inserted = 0, ret = 0;
	
	srand(seed);
	while (nodes_inserted < nr_nodes) {
		int key = rand() % max_key;

		ret = bst_add(key, (uintptr_t)1, root);
		nodes_inserted += ret;
	}

	return nodes_inserted;
}

#define MAX(a,b) ( (a) >= (b) ? (a) : (b) )
static int total_paths;
static int min_path_len, max_path_len;
static int total_nodes;
static int avl_violations, bst_violations;
static int _avl_validate_rec(node_t *root, int _depth,
                             int range_min, int range_max)
{
	if (!root)
		return -1;

	node_t *left = root->left;
	node_t *right = root->right;

	total_nodes++;
	_depth++;

	/* BST violation? */
	int lol = bst_violations;
	if (range_min != -1 && root->key < range_min)
		bst_violations++;
	if (range_max != -1 && root->key > range_max)
		bst_violations++;

	if (lol != bst_violations)
		printf("BST VIOLATION: %d (%d -> %d)\n", root->key, range_min, range_max);

	/* We found a path (a node with at least one sentinel child). */
	if (!left || !right) {
		total_paths++;

		if (_depth <= min_path_len)
			min_path_len = _depth;
		if (_depth >= max_path_len)
			max_path_len = _depth;
	}

	/* Check subtrees. */
	int lheight = -1, rheight = -1;
	if (left)
		lheight = _avl_validate_rec(left, _depth, range_min, root->key);
	if (right)
		rheight = _avl_validate_rec(right, _depth, root->key, range_max);

	/* AVL violation? */
	if (abs(lheight - rheight) > 1)
		avl_violations++;

	return MAX(lheight, rheight) + 1;
}

static inline int _avl_validate_helper(node_t *root)
{
	int check_avl = 0, check_bst = 0;
	int check = 0;
	total_paths = 0;
	min_path_len = 99999999;
	max_path_len = -1;
	total_nodes = 0;
	avl_violations = 0;
	bst_violations = 0;

	_avl_validate_rec(root, 0, -1, -1);

	check_avl = (avl_violations == 0);
	check_bst = (bst_violations == 0);
	check = (check_avl && check_bst);

	printf("Validation:\n");
	printf("=======================\n");
	printf("  Valid AVL Tree: %s\n",
	       check ? "Yes [OK]" : "No [ERROR]");
	printf("  AVL Violation: %s\n",
	       check_avl ? "No [OK]" : "Yes [ERROR]");
	printf("  BST Violation: %s\n",
	       check_bst ? "No [OK]" : "Yes [ERROR]");
	printf("  Total nodes: %d\n", total_nodes);
	printf("  Total paths: %d\n", total_paths);
	printf("  Min/max paths length: %d/%d\n", min_path_len, max_path_len);
	printf("\n");

//	avl_print_struct(root);
	return check;
}

/******************************************************************************/
/* Red-Black tree interface implementation                                    */
/******************************************************************************/
void *rbt_new()
{
	printf("Size of tree node is %lu\n", sizeof(node_t));
	return (void *)bst_initialize();
}

void *rbt_thread_data_new(int tid)
{
//	return htm_fg_tdata_new(tid);
	return NULL;
}

void rbt_thread_data_print(void *thread_data)
{
//	htm_fg_tdata_print(thread_data);
	return;
}

void rbt_thread_data_add(void *d1, void *d2, void *dst)
{
//	htm_fg_tdata_add(d1, d2, dst);
}

int rbt_lookup(void *avl, void *thread_data, int key)
{
	int ret;
	ret = bst_contains(key, avl);
	return ret;
}

int rbt_insert(void *avl, void *thread_data, int key, void *value)
{
	int ret = 0;
//	avl_node_t *nodes[2];
//
//	nodes[0] = avl_node_new(key, value);
//	nodes[1] = avl_node_new(key, value);
//
	ret = bst_add(key, (uintptr_t)1, avl);
//
//	if (!ret) {
//		free(nodes[0]);
//		free(nodes[1]);
//	}

	return ret;
}

int rbt_delete(void *avl, void *thread_data, int key)
{
	int ret = 0;
//	avl_node_t *nodes_to_delete[2] = {NULL, NULL};
//
	ret = bst_remove(key, avl);
//
////	if (ret) {
////		free(nodes_to_delete[0]);
////		free(nodes_to_delete[1]);
////	}

	return ret;
}

int rbt_validate(void *avl)
{
	int ret = 1;
	node_t *root = avl;
	ret = _avl_validate_helper(root->right);
	return ret;
}

int rbt_warmup(void *avl, int nr_nodes, int max_key, 
               unsigned int seed, int force)
{
	int ret = 0;
	ret = _avl_warmup_helper(avl, nr_nodes, max_key, seed, force);
	return ret;
}

char *rbt_name()
{
	return "avl_bronson";
}
