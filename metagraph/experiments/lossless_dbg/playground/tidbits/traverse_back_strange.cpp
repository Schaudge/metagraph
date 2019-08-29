//
// Created by Jan Studený on 2019-07-28.
//

#include "dbg_succinct.hpp"
#include "utilities.hpp"


int main() {
    vector<string> reads = {"ACTAGGA","ACTCGGA"};
    int kmer_length = 3;
    auto graph = std::make_unique<DBGSuccinct>(dbg_succ_graph_constructor(reads, kmer_length));
    int node = graph->kmer_to_node("ACT");
    PRINT_VAR(graph->traverse_back(node,'$'));
    PRINT_VAR(graph->traverse_back(1,'$'));
    graph->mask_dummy_kmers(1, false);
    PRINT_VAR(graph->traverse_back(node,'$'));
}