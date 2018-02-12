/*
 * Created by Ivo Georgiev on 2/9/16.
 * Edited: Ryan McOmber 2/11/18
 * Only this will be committed to the repository since it is the only thing that is graded (according to the assignment ReadME)
 * Couldn't test this, will do so in the future (computer (and likely user) error)
 * 
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _alloc {
    char *mem;
    size_t size;
} alloc_t, *alloc_pt;

typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {
    // ensure that it's called only once until mem_free
    // allocate the pool store with initial capacity
    // note: holds pointers only, other functions to allocate/deallocate
    if (pool_store != NULL) {
        // if != NULL, mem_init() was called (again) before mem_free
            return ALLOC_FAIL;
    }
    else {
        // allocate pool store with starting capacity
        pool_store = (pool_mgr_pt*) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));
        pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
        return ALLOC_OK; //memory is properly allocated
    }}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been deallocated
    // can free the pool store array
    // update static variables
    // if pool_store == NULL then ^ is not true 
    if (pool_store == NULL) {
        return ALLOC_FAIL;
    }
    for (int i = 0; i < pool_store_size; ++i) {
        if (pool_store[i] != NULL) {
            return ALLOC_FAIL;
        }
    }
    //Now we move on to free 
    free(pool_store);
    //Update/replace static variables, zero/null as necessary
    pool_store_capacity = 0;
    pool_store_size = 0;
    pool_store = NULL;

    return ALLOC_OK;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // make sure there the pool store is allocated
    // expand the pool store, if necessary
    if (pool_store == NULL) { 
        //no pool_store allocated
        return NULL;
    }
     alloc_status ret_status = _mem_resize_pool_store();
     assert(ret_status == ALLOC_OK); //end program if alloc not OK
     if (ret_status != ALLOC_OK) {
         return NULL; //pool store needs expanding
     }
    // allocate a new mem pool mgr
    pool_mgr_pt new_pmgr = (pool_mgr_pt) calloc(1, sizeof(pool_mgr_t));
    //check that it works
    assert(new_pmgr);
    if (new_pmgr == NULL) {
        return NULL; //didnt work, return NULL
    }
    // allocate a new memory pool
    void * new_mem = malloc(size);
    // check success, on error deallocate mgr and return null
    new_pmgr->pool.mem = new_mem;   //size bytes
    new_pmgr->pool.policy = policy;
    new_pmgr->pool.total_size = size;
    new_pmgr->pool.num_allocs = 0;  // no nodes should have been allocated
    new_pmgr->pool.num_gaps = 1;    // This is a guess
    new_pmgr->pool.alloc_size = 0;  // pool shouldn't have anything
    // allocate a new node heap
    assert(new_pmgr->pool.mem);
    // check success, on error deallocate mgr/pool and return null
    if (new_pmgr->pool.mem == NULL) {
        free(new_pmgr);
        new_pmgr = NULL;
        return NULL;
    }
    // allocate a new node heap
    node_pt new_nheap = (node_pt) calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));
    // check success, on error deallocate mgr/pool and return null
    assert(new_nheap);
    if (new_nheap == NULL) {
        free(new_pmgr);
        new_pmgr = NULL; //deallocations
        free(new_mem);
        new_mem = NULL;
        return NULL;
    }
    // allocate a new gap index
    gap_pt new_gapix = (gap_pt) calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    // check success, on error deallocate mgr/pool/heap and return null
    //assert(new_gapix);
    if (new_gapix == NULL) { //error
        free(new_pmgr);
        new_pmgr = NULL; //deallocations
        free(new_mem);
        new_mem = NULL;
        free(new_nheap);
        new_nheap = NULL;
        return NULL;
    }
    // assign all the pointers and update meta data:
    new_pmgr->node_heap = new_nheap;
    new_pmgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    new_pmgr->gap_ix = new_gapix;
    new_pmgr->used_nodes = 1;     //again, guessing
    new_pmgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;

    //initialize top node of node heap
    new_pmgr->node_heap[0].alloc_record.size = size;
    new_pmgr->node_heap[0].alloc_record.mem = new_mem;
    new_pmgr->node_heap[0].next = NULL;
    new_pmgr->node_heap[0].prev = NULL;
    new_pmgr->node_heap[0].used = 1;
    new_pmgr->node_heap[0].allocated = 0;
    //initialize top node of gap index
    new_pmgr->gap_ix[0].size = size;
    new_pmgr->gap_ix[0].node = new_pmgr->node_heap;

    //   initialize pool mgr
    //   link pool mgr to pool store
    // find the first empty position in the pool_store
    // and link the new pool mgr to that location
    for(int i = 0; i < pool_store_size; ++i) {
        if (pool_store[i] == NULL) {
            pool_store[i] = new_pmgr;
            pool_store_size++;
            break;
        }
    }
    // return the address of the mgr,to (pool_pt)
    return (pool_pt)new_pmgr;
}

alloc_status mem_pool_close(pool_pt pool) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    // possible because pool is at the top of the pool_mgr_t structure
    pool_mgr_pt new_pmgr = (pool_mgr_pt) pool;
    
    // check if this pool is allocated
    // check if it has zero allocations
    // check if pool has only one gap
    if (
       (new_pmgr == NULL) ||
       (pool->num_gaps > 1) ||
       (pool->num_gaps == 0) ||
       pool->num_allocs >= 1) {
        return ALLOC_FAIL;
    }
    // free memory pool
    free(new_pmgr->pool.mem);
    new_pmgr->pool.mem = NULL;

    // free node heap
    free(new_pmgr->node_heap);
    new_pmgr->node_heap = NULL;

    // free gap index
    free(new_pmgr->gap_ix);
    new_pmgr->gap_ix = NULL;

    // find mgr in pool store and set to null
    for(int i = 0; i < pool_store_size; ++i) {
        if (pool_store[i] == new_pmgr) {
            pool_store[i] = NULL;
            pool_store_size++;
            break;
        }
    }
    // note: don't decrement pool_store_size, because it only grows
    // free mgr
    free(new_pmgr);
    new_pmgr = NULL;
    return ALLOC_OK;
}

void * mem_new_alloc(pool_pt pool, size_t size) {
    
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt new_pmgr = (pool_mgr_pt) (pool);
    
    // check if any gaps, return null if none
    if (new_pmgr->pool.num_gaps == 0) {
        return NULL; //no gaps
    }
    // expand heap node, if necessary, quit on error
    alloc_status result =_mem_resize_node_heap(new_pmgr);
    assert(result == ALLOC_OK);
    if (ret_status != ALLOC_OK) {
         return NULL;
    // check used nodes less than/= total nodes, quit on error
    if (new_pmgr->used_nodes >= new_pmgr->total_nodes) { //as a note, I'm unsure if it should be ">" or ">=" but the verbage used implies the second. 
        return NULL;
    }
    
    node_pt new_alloc = NULL;
    if (pool->policy == FIRST_FIT) {
        
        //assumes node_heap[0] is head (Which it should be)
        new_alloc = new_pmgr->node_heap;
        for (int i = 0; i < new_pmgr->total_nodes; ++i) {
            // Used: 1, Allocated: 0 is gap
            // looking for gap who's size is > our needed
            if (new_alloc->used == 1 && new_alloc->allocated == 0 
                && size <= new_alloc->alloc_record.size) { // found it
                new_alloc->allocated = 1;
                break;
            }
            
            new_alloc = new_alloc->next;
            if (new_alloc == NULL) {
                return NULL;
            }
        }
        
    } else if (pool->policy == BEST_FIT) {
        
        // gaps sorted according to size 
        for (int i = 0; i < pool->num_gaps; ++i) {
            if (size <= new_pmgr->gap_ix[i].size) { // found gap
                new_alloc = new_pmgr->gap_ix[i].node;
                new_alloc->allocated = 1;
                break;
            }
        }
    }
    
    if (new_alloc == NULL || !new_alloc->allocated) { //node not found
        return NULL;
    }
    
    // update metadata (num_allocs, alloc_size)
    pool->num_allocs += 1;
    pool->alloc_size += size;
    
    size_t remaining_gap = new_alloc->alloc_record.size - size;
    _mem_remove_from_gap_ix(new_pmgr, new_alloc->alloc_record.size, new_alloc);
    
    // convert gap_node to an allocation node of given size
    new_alloc->alloc_record.size = size;
    
    if (remaining_gap) {
        
        node_pt new_gap = NULL;
        for (int i = 0; i < new_pmgr->total_nodes; ++i) {
            if (!new_pmgr->node_heap[i].used) {
                //unused gap
                new_gap = &new_pmgr->node_heap[i];
                new_gap->used = 1;
                new_gap->alloc_record.mem = NULL;
                new_gap->alloc_record.size = remaining_gap;
                new_gap->allocated = 0;
                
                if (new_alloc->next != NULL) { //next is empty
                    new_alloc->next->prev = new_gap;
                }
                new_gap->next = new_alloc->next;
                new_alloc->next = new_gap;
                new_gap->prev = new_alloc;
                new_pmgr->used_nodes += 1;
                break;
            }
        }
        alloc_status status = _mem_add_to_gap_ix(new_pmgr, remaining_gap, new_gap);
        if (status == ALLOC_FAIL) { //not sure if necessary or not
            return NULL;
        }
    }
    return (alloc_pt) new_alloc;
}

alloc_status mem_del_alloc(pool_pt pool, void* alloc) {
    
    pool_mgr_pt new_pmgr = (pool_mgr_pt) pool;
    node_pt node_handle = (node_pt) alloc; //new pointers
    
    printf("deleting %p\n\n", &node_handle->alloc_record.mem); //communicate to user
    
    // find the node to delete in the node heap
    unsigned del_index = 0;
    while (del_index <= new_pmgr->total_nodes) {
        
        if (new_pmgr->node_heap[del_index].alloc_record.mem == node_handle->alloc_record.mem) {
            break;
        }
        ++del_index;
    }
    
    // make sure
    if (del_index == new_pmgr->total_nodes) {
        return ALLOC_FAIL;
    }
    
    // convert to gap 
    // allocated = 0 is gap
    node_handle->allocated = 0;
    
    // update metadata (num_allocs, alloc_size)
    --pool->num_allocs; //decrement
    pool->alloc_size -= node_handle->alloc_record.size; //decrement
    
    // if the next node in the list is also a gap, merge into node handle
    if ((node_handle->next != NULL) &&
        (node_handle->next->allocated == 0)) {
        
        _mem_remove_from_gap_ix(new_pmgr,
            node_handle->next->alloc_record.size,
            node_handle->next);
        
        // add the sizes
        // update node/metadata (decrement)
        node_handle->alloc_record.size += node_handle->next->alloc_record.size;
        node_handle->next->used = 0;
        --new_pmgr->used_nodes;
        
        // update linked list:
        // IF next node has a continuing node, give
        // THAT node a new prev. We are merging the
        // node_handle->next INTO node_handle
        if (node_handle->next->next) { 
            node_handle->next->next->prev = node_handle;
        }
        node_pt tmp = node_handle->next;
        node_handle->next = node_handle->next->next;
        tmp->next = NULL; //new temporary 
        tmp->prev = NULL;
        tmp->alloc_record.size = 0;
    }
    
    // if the prev node in the list is also a gap, merge into node handle
    if ((node_handle->prev != NULL) &&
        (node_handle->prev->allocated == 0)) {
        
        alloc_status status = _mem_remove_from_gap_ix(new_pmgr,
            node_handle->prev->alloc_record.size,
            node_handle->prev);
        
        // add the sizes
        // update node as unused
        // update metadata (used nodes)
        node_handle->alloc_record.size += node_handle->prev->alloc_record.size;
        node_handle->prev->used = 0;
        --new_pmgr->used_nodes;
        
        // update linked list:
        // IF prev node has a continuing node, give
        // THAT node a new next. We are merging the
        // node_handle->prev INTO node_handle
        if (node_handle->prev->prev) {
            node_handle->prev->prev->next = node_handle;
        }
        node_pt tmp = node_handle->prev;
        node_handle->prev = node_handle->prev->prev;
        tmp->prev = NULL;
        tmp->next = NULL;
        tmp->alloc_record.size = 0;
    }
    
    alloc_status status = _mem_add_to_gap_ix(new_pmgr, node_handle->alloc_record.size, node_handle);
    
    if (status == ALLOC_FAIL) { //again, not sure this is necessary here
        return ALLOC_FAIL;
    }

    return ALLOC_OK;
}

void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {
    // get the mgr from the pool
    pool_mgr_pt new_pmgr = (pool_mgr_pt) pool;

    // allocate the segments array with size == used_nodes
    pool_segment_pt new_seg_array = calloc(new_pmgr->used_nodes, sizeof(pool_segment_t));
    if (new_seg_array == NULL) {
        return;
    }
    
    node_pt it = NULL;
    
    // Find first used node in node heap.
    for (int i = 0; i < new_pmgr->total_nodes; ++i) {
        if (new_pmgr->node_heap[i].used) {
            it = &new_pmgr->node_heap[i];
            break;
        }
    }
    
    // Traverse to the beginning of the heap
    assert(it != NULL);
    while (it->prev != NULL) {
        it = it->prev;
    }
    
    for (int i = 0; i < new_pmgr->total_nodes; ++i) {
        new_seg_array[i].size = it->alloc_record.size;
        new_seg_array[i].allocated = it->allocated;
        it = it->next;
        if (it == NULL) {
            break;
        }
    }
    
    // "return" the values
    *segments = new_seg_array;
    *num_segments = new_pmgr->used_nodes;
}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    // check if necessary
    // cast to float for accurate math with float const MEM_POOL_STORE_FILL_FACTOR
    // using a cast to size_t, otherwise the float cannot fit in size_t
    if (((float) pool_store_size / pool_store_capacity) >= MEM_POOL_STORE_FILL_FACTOR) //is necessary
    {
        pool_store = realloc(pool_store, (size_t) (pool_store_capacity *
                MEM_POOL_STORE_FILL_FACTOR * sizeof(pool_mgr_pt)));
        pool_store_capacity = pool_store_capacity * MEM_POOL_STORE_EXPAND_FACTOR;
        printf("Resized pool store\n");
    }
    return ALLOC_OK;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    
    if (((float) pool_mgr->used_nodes / pool_mgr->total_nodes) >= MEM_NODE_HEAP_FILL_FACTOR) 
    {
        //clear gap index
        printf("Resized node heap\n");
        _mem_invalidate_gap_ix(pool_mgr);
        
        // allocate a new, expanded node heap
        node_pt new_heap = calloc(pool_mgr->total_nodes *
            MEM_NODE_HEAP_EXPAND_FACTOR, sizeof(node_t));
        
        // allocate a work node, use this to traverse each next pointer in the list
        node_pt work_node = pool_mgr->node_heap;
        
        // allocate the head pointer to easily copy over at the end
        node_pt head_node = new_heap;
        //traverse and assign one at a time
        while (work_node != NULL) { //(work_node != NULL)
            memcpy(new_heap, work_node, sizeof(node_t));
            
            // if it's a gap add it to the gap index
            if (work_node->allocated == 0) {
                _mem_add_to_gap_ix(pool_mgr, new_heap->alloc_record.size, new_heap);
            }
            
            // move on to the next in the list
            work_node = work_node->next;
            new_heap = new_heap->next;
        }
        
        // update size of heap and head.
        pool_mgr->total_nodes = pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR;
        pool_mgr->node_heap = head_node;
    }
    return ALLOC_OK;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    if (( (float) pool_mgr->gap_ix->size / pool_mgr->gap_ix_capacity) >=
            MEM_GAP_IX_FILL_FACTOR) {
        pool_mgr->gap_ix = realloc(pool_mgr->gap_ix, (size_t)(pool_mgr->gap_ix_capacity *
                                                               MEM_GAP_IX_EXPAND_FACTOR * sizeof(pool_mgr_pt)));
        pool_mgr->gap_ix_capacity = pool_mgr->gap_ix_capacity * MEM_GAP_IX_EXPAND_FACTOR;
        printf("Resized gap index\n");
    }
    return ALLOC_OK;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {
    // let the result be negative to start
    alloc_status result = ALLOC_FAIL;
    if (pool_mgr->pool.num_gaps == pool_mgr->gap_ix_capacity) {
        // expand the gap index, if necessary (call the function)
        result = _mem_resize_gap_ix(pool_mgr);
        // assert(result == ALLOC_OK);
        if (result != ALLOC_OK) {
            return ALLOC_FAIL;
        }
    }
    // Set size and pointer to the node of this gap node
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = size;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = node;
    // update metadata (num_gaps)
    ++pool_mgr->pool.num_gaps;
    
    // sort the gap index (call the function)
    result = _mem_sort_gap_ix(pool_mgr);
    // check success
    if (result != ALLOC_OK) {
        return ALLOC_FAIL;
    }
    return result;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    assert(pool_mgr->pool.num_gaps != 0);
    // find the position of the node in the gap index, track that index
    unsigned i;
    for (i = 0; i < pool_mgr->pool.num_gaps; ++i) {
        if (pool_mgr->gap_ix[i].node == node) {
            break;
        }
    }
    // loop from there to the end of the array:
    //     pull the entries (i.e. copy over) one position up
    //     this effectively deletes the chosen node
    while (i < pool_mgr->pool.num_gaps - 1) {
        pool_mgr->gap_ix[i].size = pool_mgr->gap_ix[i+1].size;
        pool_mgr->gap_ix[i].node = pool_mgr->gap_ix[i+1].node;
        i++;
    }
    // update metadata (num_gaps)
    //zero out
    --pool_mgr->pool.num_gaps;
    pool_mgr->gap_ix[i].size = 0;
    pool_mgr->gap_ix[i].node = NULL;
    
    

    return ALLOC_OK;
}

// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:
    for (int i = pool_mgr->pool.num_gaps - 1; i != 0; --i) {
        if ((pool_mgr->gap_ix[i].size <= pool_mgr->gap_ix[i-1].size)
                && (pool_mgr->gap_ix[i].node <
                pool_mgr->gap_ix[i-1].node)) {
            gap_t tmp_gap = pool_mgr->gap_ix[i];
            pool_mgr->gap_ix[i] = pool_mgr->gap_ix[i-1];
            pool_mgr->gap_ix[i-1] = tmp_gap;
        }
    }
    return ALLOC_OK;
}

static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr) {
    //create a gap_ix to work from, pointing to the gap_ix we're clearing
    gap_pt new_gap = pool_mgr->gap_ix;

    //iterate through gaps and clear the data and pointer
    for (int i = 0; i < pool_mgr->pool.num_gaps; ++i) {
        new_gap[i].size = 0;
        new_gap[i].node = NULL;
    }
    // make sure we got every gap (should be impossible not to)
    for (int i = 0; i < pool_mgr->pool.num_gaps; ++i) {
        if (new_gap[i].node != NULL || new_gap[i].size != 0) {
            return ALLOC_FAIL;
        }
    }
    printf("gap index now invalid\n"); //comunicate to user
    return ALLOC_OK;
}
   
