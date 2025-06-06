#include "test_dbg_helpers.hpp"

#include "gtest/gtest.h"
#include "graph/representation/canonical_dbg.hpp"
#include "graph/representation/succinct/boss.hpp"
#include "graph/representation/succinct/boss_construct.hpp"
#include "graph/representation/bitmap/dbg_bitmap_construct.hpp"
#include "graph/graph_extensions/node_first_cache.hpp"
#include "common/seq_tools/reverse_complement.hpp"
#include <cassert>

namespace mtg {
namespace test {

using namespace mtg::graph::boss;

template <class Graph>
size_t max_test_k() {
    return 256 / kmer::KmerExtractorBOSS::bits_per_char;
}
template size_t max_test_k<DBGSuccinct>();
template size_t max_test_k<DBGSuccinctIndexed<1>>();
template size_t max_test_k<DBGSuccinctIndexed<2>>();
template size_t max_test_k<DBGSuccinctIndexed<10>>();
template size_t max_test_k<DBGSuccinctBloomFPR<1, 1>>();
template size_t max_test_k<DBGSuccinctBloomFPR<1, 10>>();
template size_t max_test_k<DBGSuccinctBloom<4, 1>>();
template size_t max_test_k<DBGSuccinctBloom<4, 50>>();
template size_t max_test_k<DBGSuccinctCached>();

template<> size_t max_test_k<DBGBitmap>() {
    return 63. / kmer::KmerExtractor2Bit().bits_per_char;
}
template<> size_t max_test_k<DBGHashOrdered>() {
    return 256 / kmer::KmerExtractor2Bit::bits_per_char;
}
template<> size_t max_test_k<DBGHashFast>() {
    return 256 / kmer::KmerExtractor2Bit::bits_per_char;
}
template<> size_t max_test_k<DBGHashString>() {
    return 100;
}
template<> size_t max_test_k<DBGSSHash>() {
    return 255 / DBGSSHash::kmer_t<uint64_t>::bits_per_char;
}

template <class Graph>
std::vector<std::string>
get_primary_contigs(size_t k, const std::vector<std::string> &sequences) {
    auto graph = build_graph_batch<Graph>(k, sequences, DeBruijnGraph::CANONICAL);

    std::vector<std::string> contigs;
    graph->call_sequences([&](const std::string &contig, const auto &) {
        contigs.push_back(contig);
    }, 1, true);

    return contigs;
}

template <class Graph>
std::shared_ptr<DeBruijnGraph>
build_graph(uint64_t k,
            std::vector<std::string> sequences,
            DeBruijnGraph::Mode mode) {
    if (mode == DeBruijnGraph::PRIMARY)
        sequences = get_primary_contigs<Graph>(k, sequences);

    auto graph = std::make_shared<Graph>(k, mode);

    uint64_t max_index = graph->max_index();

    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence, [&](auto i) { ASSERT_TRUE(i <= ++max_index); });
    }

    [&]() { ASSERT_EQ(max_index, graph->max_index()); }();

    if (mode == DeBruijnGraph::PRIMARY)
        return std::make_shared<CanonicalDBG>(
                std::static_pointer_cast<DeBruijnGraph>(graph), 2 /* cache size */);

    return graph;
}

template
std::shared_ptr<DeBruijnGraph>
build_graph<DBGHashOrdered>(uint64_t, std::vector<std::string>, DeBruijnGraph::Mode);

template
std::shared_ptr<DeBruijnGraph>
build_graph<DBGHashFast>(uint64_t, std::vector<std::string>, DeBruijnGraph::Mode);

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGHashString>(uint64_t k,
                           std::vector<std::string> sequences,
                           DeBruijnGraph::Mode) {
    auto graph = std::make_shared<DBGHashString>(k);

    uint64_t max_index = graph->max_index();

    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence, [&](auto i) { ASSERT_TRUE(i <= ++max_index); });
    }

    [&]() { ASSERT_EQ(max_index, graph->max_index()); }();

    return graph;
}



template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGBitmap>(uint64_t k,
                       std::vector<std::string> sequences,
                       DeBruijnGraph::Mode mode) {
    if (mode == DeBruijnGraph::PRIMARY)
        sequences = get_primary_contigs<DBGBitmap>(k, sequences);

    DBGBitmapConstructor constructor(k, mode);
    for (const auto &sequence : sequences) {
        constructor.add_sequence(std::string(sequence));
    }

    auto graph = std::make_shared<DBGBitmap>(&constructor);

    if (mode == DeBruijnGraph::PRIMARY)
        return std::make_shared<CanonicalDBG>(
                std::static_pointer_cast<DeBruijnGraph>(graph), 2 /* cache size */);

    return graph;
}

void writeFastaFile(const std::vector<std::string>& sequences, const std::string& outputFilename) {
    std::ofstream fastaFile(outputFilename);

    if (!fastaFile.is_open()) {
        common::logger->error("Unable to open output file {}", outputFilename);
        return;
    }

    for (size_t i = 0; i < sequences.size(); ++i) {
        fastaFile << ">" << "\n" << sequences[i] << "\n";
    }

    fastaFile.close();
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSSHash>(uint64_t k,
                       std::vector<std::string> sequences,
                       DeBruijnGraph::Mode mode) {
    if (sequences.empty())
        return std::make_shared<DBGSSHash>(k, mode);

    // use DBGHashFast to get contigs for SSHash
    auto string_graph = build_graph<DBGHashFast>(k, sequences, mode);

    std::vector<std::string> contigs;
    size_t num_kmers = 0;
    size_t num_chars = 0;
    string_graph->call_sequences([&](const std::string &contig, const auto &path) {
        contigs.push_back(contig);
        num_kmers += path.size();
        num_chars += contig.size();
    }, 1, mode != DeBruijnGraph::BASIC);

    if (contigs.empty())
        return std::make_shared<DBGSSHash>(k, mode);

    std::string dump_path = "../tests/data/sshash_sequences/contigs.fa";
    writeFastaFile(contigs, dump_path);

    std::shared_ptr<DBGSSHash> graph;
    graph = std::make_shared<DBGSSHash>(dump_path, k, mode, num_chars);

    if (mode == DeBruijnGraph::PRIMARY)
        return std::make_shared<CanonicalDBG>(
                std::static_pointer_cast<DeBruijnGraph>(graph), 2 /* cache size */);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinct>(uint64_t k,
                         std::vector<std::string> sequences,
                         DeBruijnGraph::Mode mode) {
    if (mode == DeBruijnGraph::PRIMARY)
        sequences = get_primary_contigs<DBGSuccinct>(k, sequences);

    auto graph = std::make_shared<DBGSuccinct>(k, mode);

    uint64_t max_index = graph->max_index();

    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence, [&](auto i) { ASSERT_TRUE(i <= ++max_index); });
    }

    [&]() { ASSERT_EQ(max_index, graph->max_index()); }();

    graph->mask_dummy_kmers(1, false);

    if (mode == DeBruijnGraph::PRIMARY)
        return std::make_shared<CanonicalDBG>(
                std::static_pointer_cast<DeBruijnGraph>(graph), 2 /* cache size */);

    return graph;
}

// Cast DeBruijnGraph to DBGSuccinct. Also works for graphs wrapped into CanonicalDBG
DBGSuccinct& get_dbg_succ(DeBruijnGraph &graph) {
    const DeBruijnGraph *g = &graph;
    if (const auto *canonical = dynamic_cast<const CanonicalDBG*>(g))
        g = &canonical->get_graph();

    return const_cast<DBGSuccinct&>(dynamic_cast<const DBGSuccinct&>(*g));
}

BOSS& get_boss(DeBruijnGraph &graph) {
    return const_cast<BOSS&>(get_dbg_succ(graph).get_boss());
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctIndexed<1>>(uint64_t k,
                                   std::vector<std::string> sequences,
                                   DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    BOSS &boss = get_boss(*graph);
    boss.index_suffix_ranges(1);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctIndexed<2>>(uint64_t k,
                                   std::vector<std::string> sequences,
                                   DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    BOSS &boss = get_boss(*graph);
    boss.index_suffix_ranges(std::min(k - 1, (uint64_t)2));

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctIndexed<10>>(uint64_t k,
                                    std::vector<std::string> sequences,
                                    DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    BOSS &boss = get_boss(*graph);
    boss.index_suffix_ranges(std::min(k - 1, (uint64_t)10));

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctBloomFPR<1, 1>>(uint64_t k,
                                       std::vector<std::string> sequences,
                                       DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter_from_fpr(1.0);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctBloomFPR<1, 10>>(uint64_t k,
                                        std::vector<std::string> sequences,
                                        DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter_from_fpr(1.0 / 10);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctBloom<4, 1>>(uint64_t k,
                                    std::vector<std::string> sequences,
                                    DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter(4.0, 1);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctBloom<4, 50>>(uint64_t k,
                                     std::vector<std::string> sequences,
                                     DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter(4.0, 50);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph<DBGSuccinctCached>(uint64_t k,
                               std::vector<std::string> sequences,
                               DeBruijnGraph::Mode mode) {
    auto graph = build_graph<DBGSuccinct>(k, sequences, mode);
    if (mode == DeBruijnGraph::PRIMARY)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(get_dbg_succ(*graph)));

    return graph;
}


template <class Graph>
std::shared_ptr<DeBruijnGraph>
build_graph_batch(uint64_t k,
                  std::vector<std::string> sequences,
                  DeBruijnGraph::Mode mode) {
    return build_graph<Graph>(k, sequences, mode);
}

template
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGHashOrdered>(uint64_t, std::vector<std::string>, DeBruijnGraph::Mode);

template
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGHashFast>(uint64_t, std::vector<std::string>, DeBruijnGraph::Mode);

template
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGHashString>(uint64_t, std::vector<std::string>, DeBruijnGraph::Mode);

template
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSSHash>(uint64_t, std::vector<std::string>, DeBruijnGraph::Mode);

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGBitmap>(uint64_t k,
                             std::vector<std::string> sequences,
                             DeBruijnGraph::Mode mode) {
    if (mode == DeBruijnGraph::PRIMARY)
        sequences = get_primary_contigs<DBGBitmap>(k, sequences);

    DBGBitmapConstructor constructor(k, mode);
    constructor.add_sequences(std::vector<std::string>(sequences));
    auto graph = std::make_shared<DBGBitmap>(&constructor);

    if (mode == DeBruijnGraph::PRIMARY)
        return std::make_shared<CanonicalDBG>(
                std::static_pointer_cast<DeBruijnGraph>(graph), 2 /* cache size */);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinct>(uint64_t k,
                               std::vector<std::string> sequences,
                               DeBruijnGraph::Mode mode) {
    if (mode == DeBruijnGraph::PRIMARY)
        sequences = get_primary_contigs<DBGSuccinct>(k, sequences);

    BOSSConstructor constructor(k - 1, mode == DeBruijnGraph::CANONICAL);
    EXPECT_EQ(k - 1, constructor.get_k());
    constructor.add_sequences(std::vector<std::string>(sequences));
    auto graph = std::make_shared<DBGSuccinct>(new BOSS(&constructor), mode);
    graph->mask_dummy_kmers(1, false);
    EXPECT_EQ(k, graph->get_k());

    if (mode == DeBruijnGraph::PRIMARY)
        return std::make_shared<CanonicalDBG>(
                std::static_pointer_cast<DeBruijnGraph>(graph), 2 /* cache size */);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctIndexed<1>>(uint64_t k,
                                         std::vector<std::string> sequences,
                                         DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    BOSS &boss = get_boss(*graph);
    boss.index_suffix_ranges(1);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctIndexed<2>>(uint64_t k,
                                         std::vector<std::string> sequences,
                                         DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    BOSS &boss = get_boss(*graph);
    boss.index_suffix_ranges(std::min(k - 1, (uint64_t)2));

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctIndexed<10>>(uint64_t k,
                                          std::vector<std::string> sequences,
                                          DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    BOSS &boss = get_boss(*graph);
    boss.index_suffix_ranges(std::min(k - 1, (uint64_t)10));

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctBloomFPR<1, 1>>(uint64_t k,
                                             std::vector<std::string> sequences,
                                             DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter_from_fpr(1.0);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctBloomFPR<1, 10>>(uint64_t k,
                                              std::vector<std::string> sequences,
                                              DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter_from_fpr(1.0 / 10);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctBloom<4, 1>>(uint64_t k,
                                          std::vector<std::string> sequences,
                                          DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter(4.0, 1);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctBloom<4, 50>>(uint64_t k,
                                           std::vector<std::string> sequences,
                                           DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    DBGSuccinct &dbg_succ = get_dbg_succ(*graph);
    dbg_succ.initialize_bloom_filter(4.0, 50);

    return graph;
}

template <>
std::shared_ptr<DeBruijnGraph>
build_graph_batch<DBGSuccinctCached>(uint64_t k,
                                     std::vector<std::string> sequences,
                                     DeBruijnGraph::Mode mode) {
    auto graph = build_graph_batch<DBGSuccinct>(k, sequences, mode);
    if (mode == DeBruijnGraph::PRIMARY)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(get_dbg_succ(*graph)));

    return graph;
}


template <class Graph>
bool check_graph(const std::string &alphabet, DeBruijnGraph::Mode mode, bool check_sequence) {
    std::vector<std::string> sequences;

    for (size_t i = 0; i < 100; ++i) {
        std::string seq(1'000, 'A');
        for (size_t j = 0; j < seq.size(); ++j) {
            seq[j] = alphabet[(i * i + j + 17 * j * j) % alphabet.size()];
        }
        sequences.push_back(seq);
    }

#if _PROTEIN_GRAPH
    auto graph = build_graph<Graph>(12, sequences, mode);
#else
    auto graph = build_graph<Graph>(20, sequences, mode);
#endif

    bool node_remap_failed = false;
    graph->call_nodes(
        [&graph, &node_remap_failed](DeBruijnGraph::node_index i) {
            if (graph->kmer_to_node(graph->get_node_sequence(i)) != i) {
                node_remap_failed = true;
                std::cerr << "Node failed\n"
                          << i << " " << graph->get_node_sequence(i) << "\n"
                          << graph->kmer_to_node(graph->get_node_sequence(i)) << " "
                          << graph->get_node_sequence(graph->kmer_to_node(graph->get_node_sequence(i))) << "\n";
            }
        },
        [&node_remap_failed]() { return node_remap_failed; }
    );
    if (node_remap_failed)
        return false;

    if (!check_sequence)
        return true;

    for (const auto &seq : sequences) {
        bool stop = false;
        graph->map_to_nodes(
            seq,
            [&](const auto &i) {
                stop = !i || graph->kmer_to_node(graph->get_node_sequence(i)) != i;
            },
            [&]() { return stop; }
        );

        if (stop)
            return false;
    }

    return true;
}

template bool check_graph<DBGSuccinct>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctIndexed<1>>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctIndexed<2>>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctIndexed<10>>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctBloomFPR<1, 1>>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctBloomFPR<1, 10>>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctBloom<4, 1>>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctBloom<4, 50>>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSuccinctCached>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGBitmap>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGHashOrdered>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGHashFast>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGHashString>(const std::string &, DeBruijnGraph::Mode, bool);
template bool check_graph<DBGSSHash>(const std::string &, DeBruijnGraph::Mode, bool);

} // namespace test
} // namespace mtg
