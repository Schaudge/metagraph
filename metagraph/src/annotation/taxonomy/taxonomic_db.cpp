#include "taxonomic_db.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include "common/serialization.hpp"
#include "seq_io/sequence_io.hpp"
#include "common/utils/string_utils.hpp"
#include "common/unix_tools.hpp"

#include "common/logger.hpp"


namespace mtg {
namespace annot {

using mtg::common::logger;

typedef TaxonomyDB::NormalizedTaxId NormalizedTaxId;

void TaxonomyDB::dfs_statistics(const NormalizedTaxId &node, const ChildrenList &tree,
                                   std::vector<NormalizedTaxId> &tree_linearization) {
    node_to_linearization_idx[node] = tree_linearization.size();
    tree_linearization.push_back(node);
    uint64_t depth = 0;
    for(const auto &child: tree[node]) {
        dfs_statistics(child, tree, tree_linearization);
        tree_linearization.push_back(node);
        if (node_depth[child] > depth) {
            depth = node_depth[child];
        }
    }
    node_depth[node] = depth + 1;
}

void TaxonomyDB::read_tree(const std::string &taxo_tree_filepath,
                           const tsl::hopscotch_map<TaxId, AccessionVersion> &reversed_lookup_table,
                           ChildrenList &tree, NormalizedTaxId &root_node) {
    std::ifstream f(taxo_tree_filepath);
    std::string line;

    tsl::hopscotch_map<TaxId, TaxId> full_parents_list;
    while (getline(f, line) ) {
        std::vector<std::string> parts = utils::split_string(line, "\t");
        uint64_t act = static_cast<uint64_t>(std::stoll(parts[0]));
        uint64_t parent = static_cast<uint64_t>(std::stoll(parts[2]));

        assert(!full_parents_list.count(act));
        full_parents_list[act] = parent;
    }
    f.close();

    std::queue<TaxId> relevant_taxids;
    for (const auto &it: reversed_lookup_table) {
        relevant_taxids.push(it.first);
    }

    tsl::hopscotch_map<TaxId, NormalizedTaxId> normalized_taxid;
    uint64_t num_nodes = 0;
    // num_taxid_failed used for logging only.
    uint64_t num_taxid_failed = 0;
    while (relevant_taxids.size()) {
        const TaxId taxid = relevant_taxids.front();
        relevant_taxids.pop();
        if (normalized_taxid.count(taxid)) {
            continue;
        }
        if (! full_parents_list.count(taxid)) {
            logger->warn("Taxid {} cannot be found in the taxonomic tree.", taxid);
            num_taxid_failed += 1;
            continue;
        }
        normalized_taxid[taxid] = num_nodes++;
        node_to_acc_version.push_back(reversed_lookup_table.at(taxid));
        if (taxid == full_parents_list[taxid]) {
            root_node = normalized_taxid[taxid];
            continue;
        }
        relevant_taxids.push(full_parents_list[taxid]);
        // Check if taxid is a leaf and used in fasta header.
        if (reversed_lookup_table.count(taxid)) {
            lookup_table[reversed_lookup_table.at(taxid)] = normalized_taxid.at(taxid);
        }
    }
    if (num_taxid_failed) {
        logger->warn("Number taxids succeeded {}, failed {}.", num_nodes, num_taxid_failed);
    }

    tree.resize(num_nodes);
    for (const auto &it: normalized_taxid) {
        TaxId taxid = it.first;
        if (normalized_taxid[taxid] == root_node) {
            continue;
        }
        tree[normalized_taxid[full_parents_list[taxid]]].push_back(
                normalized_taxid[taxid]);
    }
}

void TaxonomyDB::rmq_preprocessing(const std::vector<NormalizedTaxId> &tree_linearization) {
    uint num_rmq_rows = log2(tree_linearization.size()) + 1;
    rmq_data.resize(num_rmq_rows);
    for (uint64_t i = 0; i < num_rmq_rows; ++i) {
        rmq_data[i].resize(tree_linearization.size());
    }

    // Copy tree_linearization on rmq[0].
    for (uint64_t i = 0; i < tree_linearization.size(); ++i) {
        rmq_data[0][i] = tree_linearization[i];
    }

    uint delta = 1;
    for (uint row = 1; row < num_rmq_rows; ++row) {
        for (uint64_t i = 0; i + delta < tree_linearization.size(); ++i) {
            if (node_depth[rmq_data[row-1][i]] > node_depth[rmq_data[row-1][i + delta]]) {
                rmq_data[row][i] = rmq_data[row-1][i];
            } else {
                rmq_data[row][i] = rmq_data[row-1][i + delta];
            }
        }
        delta *= 2;
    }

    precalc_log2.resize(tree_linearization.size());
    precalc_pow2.push_back(1);
    for (uint64_t i = 2; i < tree_linearization.size(); ++i) {
        precalc_log2[i] = 1 + precalc_log2[i/2];
        if(precalc_log2[i] > precalc_log2[i-1]) {
            precalc_pow2.push_back(i);
        }
    }
}

void TaxonomyDB::get_input_accessions(const std::string &fasta_headers_filepath,
                                      tsl::hopscotch_set<AccessionVersion> &input_accessions) {
    std::ifstream f(fasta_headers_filepath);
    std::string line;

    while (getline(f, line) ) {
        std::string fasta_header = utils::split_string(line, "\t")[0];
        std::string accession_version = utils::split_string(fasta_header, "|")[3];
        input_accessions.insert(accession_version);
    }
    f.close();
}

// TODO improve this by parsing ".gz" directly (or https://github.com/pmenzel/taxonomy-tools)
void TaxonomyDB::read_lookup_table(const std::string &lookup_table_filepath,
                                   const tsl::hopscotch_set<AccessionVersion> &input_accessions,
                                   tsl::hopscotch_map<TaxId, AccessionVersion> &reversed_lookup_table) {
    std::ifstream f(lookup_table_filepath);
    std::string line;
    while (getline(f, line) ) {
        std::vector<std::string> parts = utils::split_string(line, "\t");
        assert(parts.size() == 4);
        if(input_accessions.count(parts[1])) {
            reversed_lookup_table[std::stoull(parts[2])] = parts[1];
        }
    }
    f.close();
}

TaxonomyDB::TaxonomyDB(const std::string &taxo_tree_filepath,
                       const std::string &lookup_table_filepath,
                       const std::string &fasta_headers_filepath) {

    if (!std::filesystem::exists(taxo_tree_filepath)) {
        logger->error("Can't open taxonomic tree file '{}'.", taxo_tree_filepath);
        std::exit(1);
    }
    if (!std::filesystem::exists(lookup_table_filepath)) {
        logger->error("Can't open taxonomic lookup table file '{}'.", lookup_table_filepath);
        std::exit(1);
    }
    if (!std::filesystem::exists(fasta_headers_filepath)) {
        logger->error("Can't open fasta headers file '{}'.", fasta_headers_filepath);
        std::exit(1);
    }
    Timer timer;
    logger->trace("Parsing fasta headers...");
    tsl::hopscotch_set<AccessionVersion> input_accessions;
    get_input_accessions(fasta_headers_filepath, input_accessions);
    logger->trace("Finished parsing fasta headers in '{}' sec", timer.elapsed());

    timer.reset();
    logger->trace("Parsing lookup table..");
    tsl::hopscotch_map<TaxId, AccessionVersion> reversed_lookup_table;
    read_lookup_table(lookup_table_filepath, input_accessions, reversed_lookup_table);
    logger->trace("Finished parsing tookup table in '{}' sec", timer.elapsed());

    timer.reset();
    logger->trace("Parsing taxonomic tree..");
    TaxonomyDB::ChildrenList tree;
    NormalizedTaxId root_node;
    read_tree(taxo_tree_filepath, reversed_lookup_table, tree, root_node);
    logger->trace("Finished parsing taxonomic tree in '{}' sec", timer.elapsed());

    timer.reset();
    logger->trace("Calculating tree statistics..");
    std::vector<NormalizedTaxId> tree_linearization;
    node_to_linearization_idx.resize(tree.size());
    node_depth.resize(tree.size());
    dfs_statistics(root_node, tree, tree_linearization);
    logger->trace("Finished calculating tree statistics in '{}' sec", timer.elapsed());

    timer.reset();
    logger->trace("Starting rmq preprocessing..");
    rmq_preprocessing(tree_linearization);
    logger->trace("Finished rmq preprocessing in '{}' sec", timer.elapsed());
}

NormalizedTaxId TaxonomyDB::find_lca(const std::vector<NormalizedTaxId> &taxids) {
    uint64_t left_idx = node_to_linearization_idx[taxids[0]];
    uint64_t right_idx = node_to_linearization_idx[taxids[0]];
    for (const auto &taxid: taxids) {
        if (node_to_linearization_idx[taxid] < left_idx) {
            left_idx = node_to_linearization_idx[taxid];
        }
        if (node_to_linearization_idx[taxid] > right_idx) {
            right_idx = node_to_linearization_idx[taxid];
        }
    }

    uint64_t log_dist = precalc_log2[right_idx - left_idx];
    uint64_t left_lca = rmq_data[log_dist][left_idx];
    uint64_t right_lca = rmq_data[log_dist][right_idx - precalc_pow2[log_dist] + 1];

    if (node_depth[left_lca] > node_depth[right_lca]) {
        return left_lca;
    }
    return right_lca;
}

bool TaxonomyDB::find_lca(const std::vector<std::string> &fasta_headers,
                          NormalizedTaxId &lca) {
    num_external_lca_calls += 1;
    std::vector<NormalizedTaxId> taxids;
    for (const auto &label: fasta_headers) {
        std::string accession_version = utils::split_string(label, "|")[3];
        if (! lookup_table.count(accession_version)) {
            logger->warn("Accession version {} cannot be found in the lookup_table.",
                         accession_version);
            num_external_lca_calls_failed += 1;
            return false;
        }
        taxids.push_back(lookup_table[accession_version]);
    }
    lca = find_lca(taxids);
    return true;
}

NormalizedTaxId TaxonomyDB::find_lca(const NormalizedTaxId &taxid1, const NormalizedTaxId &taxid2) {
    return find_lca(std::vector<NormalizedTaxId> {taxid1, taxid2});
}

void TaxonomyDB::update_taxonomic_map(const std::vector<KmerId> &kmers,
                                      const NormalizedTaxId &lca) {
    for (const auto &kmer: kmers) {
        if (taxonomic_map.count(kmer)) {
            taxonomic_map[kmer] = find_lca(taxonomic_map[kmer], lca);
        } else {
            taxonomic_map[kmer] = lca;
        }
    }
}

void TaxonomyDB::export_to_file(const std::string &filepath) {
    if (num_external_lca_calls_failed) {
        logger->warn("Total number external LCA calls: {} from which nonexistent accession versions: {}",
                     num_external_lca_calls, num_external_lca_calls_failed);
    }

    Timer timer;
    logger->trace("Exporting metagraph taxonomic data..");

    std::ofstream f(filepath.c_str(), std::ios::out | std::ios::binary);
    if (!f.is_open()) {
        logger->error("Can't open taxonomic file '{}'.", filepath.c_str());
        std::exit(1);
    }

    serialize_number_number_map(f, taxonomic_map);
    serialize_string_vector(f, node_to_acc_version);

    uint64_t num_nodes = node_to_acc_version.size();
    std::vector<bool> visited_node(num_nodes);
    std::vector<NormalizedTaxId> node_parent(num_nodes);
    const std::vector<NormalizedTaxId> &linearization = rmq_data[0];

    visited_node[linearization[0]] = true;
    node_parent[linearization[0]] = linearization[0];
    for (uint64_t i = 1; i < linearization.size(); ++i) {
        uint64_t act = linearization[i];
        uint64_t prv = linearization[i - 1];

        if (visited_node[act]) {
            // Prv is act's child.
            continue;
        }
        // Prv is act's parent.
        node_parent[act] = prv;
        visited_node[act] = true;
    }
    serialize_number_vector_raw(f, node_parent);
    f.close();
    logger->trace("Finished exporting metagraph taxonomic data after '{}' sec", timer.elapsed());
}

}
}