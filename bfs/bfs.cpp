#include "bfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstddef>
#include <omp.h>

#include "../common/CycleTimer.h"
#include "../common/graph.h"

#define ROOT_NODE_ID 0
#define NOT_VISITED_MARKER -1

void vertex_set_clear(vertex_set* list) {
    list->count = 0;
}

void vertex_set_init(vertex_set* list, int count) {
    list->max_vertices = count;
    list->vertices = (int*)malloc(sizeof(int) * list->max_vertices);
    vertex_set_clear(list);
}

// Take one step of "top-down" BFS.  For each vertex on the frontier,
// follow all outgoing edges, and add all neighboring vertices to the
// new_frontier.
void top_down_step(
    Graph g,
    vertex_set* frontier,
    vertex_set* new_frontier,
    int* distances,
    int cur_depth)
{
    #pragma omp parallel
    {
        int num = 0;
        int* nodes = new int[g->num_nodes];
        #pragma omp for schedule(dynamic, 1024)
        for (int i = 0; i < frontier->count; i++) {
            int node = frontier->vertices[i];

            int start_edge = g->outgoing_starts[node];
            int end_edge = (node == g->num_nodes - 1)
                               ? g->num_edges
                               : g->outgoing_starts[node + 1];

            // attempt to add all neighbors to the new frontier
            for (int neighbor=start_edge; neighbor<end_edge; neighbor++) {
                int outgoing = g->outgoing_edges[neighbor];

                if (distances[outgoing] == NOT_VISITED_MARKER &&
                    __sync_bool_compare_and_swap(&distances[outgoing], NOT_VISITED_MARKER, cur_depth + 1)) {
                    nodes[num++] = outgoing;
                }
            }
        }
        int offset = __sync_fetch_and_add(&new_frontier->count, num);
        memcpy(new_frontier->vertices + offset, nodes, sizeof(int) * num);
        delete [] nodes;
    }
}

// Implements top-down BFS.
//
// Result of execution is that, for each node in the graph, the
// distance to the root is stored in sol.distances.
void bfs_top_down(Graph graph, solution* sol) {

    vertex_set list1;
    vertex_set list2;
    vertex_set_init(&list1, graph->num_nodes);
    vertex_set_init(&list2, graph->num_nodes);

    vertex_set* frontier = &list1;
    vertex_set* new_frontier = &list2;

    // initialize all nodes to NOT_VISITED
    #pragma omp parallel for
    for (int i = 0; i < graph->num_nodes; i++)
        sol->distances[i] = NOT_VISITED_MARKER;

    // setup frontier with the root node
    frontier->vertices[frontier->count++] = ROOT_NODE_ID;
    sol->distances[ROOT_NODE_ID] = 0;
    int cur_depth = 0;

    while (frontier->count != 0) {

#ifdef VERBOSE
        double start_time = CycleTimer::currentSeconds();
#endif

        vertex_set_clear(new_frontier);

        top_down_step(graph, frontier, new_frontier, sol->distances, cur_depth);

#ifdef VERBOSE
    double end_time = CycleTimer::currentSeconds();
    printf("frontier=%-10d %.4f sec\n", frontier->count, end_time - start_time);
#endif

        // swap pointers
        vertex_set* tmp = frontier;
        frontier = new_frontier;
        new_frontier = tmp;

        cur_depth++;
    }

    free(frontier->vertices);
    free(new_frontier->vertices);
}


int bottom_up_step(
    Graph g,
    bool* frontier,
    bool* new_frontier,
    int* distances,
    int cur_depth)
{
    int num = 0;
    #pragma omp parallel for reduction(+:num) schedule(dynamic, 1024)
    for (int node = 0; node < g->num_nodes; node++) {
        if (distances[node] != NOT_VISITED_MARKER)
            continue;

        int start_edge = g->incoming_starts[node];
        int end_edge = (node == g->num_nodes - 1)
                           ? g->num_edges
                           : g->incoming_starts[node + 1];

        for (int neighbor = start_edge; neighbor < end_edge; neighbor++) {
            int incoming = g->incoming_edges[neighbor];

            if (frontier[incoming]) {
                distances[node] = cur_depth + 1;
                new_frontier[node] = true;
                num++;
                break;
            }
        }
    }
    return num;
}



void bfs_bottom_up(Graph graph, solution* sol)
{
    // CS149 students:
    //
    // You will need to implement the "bottom up" BFS here as
    // described in the handout.
    //
    // As a result of your code's execution, sol.distances should be
    // correctly populated for all nodes in the graph.
    //
    // As was done in the top-down case, you may wish to organize your
    // code by creating subroutine bottom_up_step() that is called in
    // each step of the BFS process.

    // 用哈希表表示边界
    bool* frontier = new bool[graph->num_nodes];
    bool* new_frontier = new bool[graph->num_nodes];

    #pragma omp parallel for
    for (int i = 0; i < graph->num_nodes; ++i) {
        sol->distances[i] = NOT_VISITED_MARKER;
        frontier[i] = false;
        new_frontier[i] = false;
    }

    frontier[ROOT_NODE_ID] = true;
    sol->distances[ROOT_NODE_ID] = 0;
    int cur_depth = 0;
    int frontier_nodes_num = 1;

    while (frontier_nodes_num) {
#ifdef VERBOSE
        double start_time = CycleTimer::currentSeconds();
#endif

        #pragma omp parallel for
        for (int i = 0; i < graph->num_nodes; ++i)
            new_frontier[i] = false;

        frontier_nodes_num = bottom_up_step(graph, frontier, new_frontier, sol->distances, cur_depth);

#ifdef VERBOSE
        double end_time = CycleTimer::currentSeconds();
        printf("frontier=%-10d %.4f sec\n", frontier_nodes_num, end_time - start_time);
#endif

        bool* tmp = frontier;
        frontier = new_frontier;
        new_frontier = tmp;

        cur_depth++;
    }

    delete[] frontier;
    delete[] new_frontier;
}

int vertex_set2hash_table(vertex_set* vertex_set_frontier, bool* hash_table_frontier) {
    int num = 0;
    #pragma omp parallel for reduction(+:num)
    for (int i = 0; i < vertex_set_frontier->count; ++i) {
        hash_table_frontier[vertex_set_frontier->vertices[i]] = true;
        num++;
    }
    return num;
}

void hash_table2vertex_set(bool* hash_table_frontier, vertex_set* vertex_set_frontier) {
    #pragma omp parallel
    {
        int num = 0;
        int* nodes = new int[vertex_set_frontier->max_vertices];
        #pragma omp for
        for (int i = 0; i < vertex_set_frontier->max_vertices; i++) {
            if (hash_table_frontier[i])
                nodes[num++] = i;
        }
        int offset = __sync_fetch_and_add(&vertex_set_frontier->count, num);
        memcpy(vertex_set_frontier->vertices + offset, nodes, sizeof(int) * num);
        delete [] nodes;
    }
}

void bfs_hybrid(Graph graph, solution* sol)
{
    // CS149 students:
    //
    // You will need to implement the "hybrid" BFS here as
    // described in the handout.
    vertex_set list1;
    vertex_set list2;
    vertex_set_init(&list1, graph->num_nodes);
    vertex_set_init(&list2, graph->num_nodes);

    vertex_set* vertex_set_frontier = &list1;
    vertex_set* vertex_set_new_frontier = &list2;

    bool* hash_table_frontier = new bool[graph->num_nodes];
    bool* hash_table_new_frontier = new bool[graph->num_nodes];

    #pragma omp parallel for
    for (int i = 0; i < graph->num_nodes; ++i) {
        sol->distances[i] = NOT_VISITED_MARKER;
        hash_table_frontier[i] = false;
        hash_table_new_frontier[i] = false;
    }

    vertex_set_frontier->vertices[vertex_set_frontier->count++] = ROOT_NODE_ID;
    sol->distances[ROOT_NODE_ID] = 0;
    int cur_depth = 0;
    int frontier_nodes_num = 1;

    bool top_downing = true;
    int threshold = graph->num_nodes * 0.1; // 1000000

    while (frontier_nodes_num) {
#ifdef VERBOSE
        double start_time = CycleTimer::currentSeconds();
#endif

        if (frontier_nodes_num < threshold) {
            if (!top_downing) {
                top_downing = true;
                vertex_set_clear(vertex_set_frontier);
                hash_table2vertex_set(hash_table_frontier, vertex_set_frontier);
            }

            vertex_set_clear(vertex_set_new_frontier);
            top_down_step(graph, vertex_set_frontier, vertex_set_new_frontier, sol->distances, cur_depth);
            frontier_nodes_num = vertex_set_new_frontier->count;

            vertex_set* tmp = vertex_set_frontier;
            vertex_set_frontier = vertex_set_new_frontier;
            vertex_set_new_frontier = tmp;
        } else {
            if (top_downing) {
                top_downing = false;
                #pragma omp parallel for
                for (int i = 0; i < graph->num_nodes; ++i)
                    hash_table_frontier[i] = false;
                frontier_nodes_num = vertex_set2hash_table(vertex_set_frontier, hash_table_frontier);
            }
            #pragma omp parallel for
            for (int i = 0; i < graph->num_nodes; ++i)
                hash_table_new_frontier[i] = false;
            frontier_nodes_num = bottom_up_step(graph, hash_table_frontier, hash_table_new_frontier, sol->distances, cur_depth);

            bool* tmp = hash_table_frontier;
            hash_table_frontier = hash_table_new_frontier;
            hash_table_new_frontier = tmp;
        }

#ifdef VERBOSE
        double end_time = CycleTimer::currentSeconds();
        printf("frontier=%-10d %.4f sec\n", vertex_set_frontier->count, end_time - start_time);
#endif

        cur_depth++;
    }

    free(vertex_set_frontier->vertices);
    free(vertex_set_new_frontier->vertices);
    delete[] hash_table_frontier;
    delete[] hash_table_new_frontier;
}
