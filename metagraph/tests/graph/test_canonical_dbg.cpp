#include "gtest/gtest.h"

#include "../test_helpers.hpp"
#include "all/test_dbg_helpers.hpp"

#include "common/seq_tools/reverse_complement.hpp"
#include "graph/representation/canonical_dbg.hpp"
#include "graph/graph_extensions/node_first_cache.hpp"
#include "graph/alignment/alignment.hpp"


namespace {

#if ! _PROTEIN_GRAPH

using namespace mtg;
using namespace mtg::test;

const std::string test_data_dir = "../tests/data";
const std::string test_dump_basename = test_data_dir + "/dump_test_graph";


inline DeBruijnGraph::node_index get_rev_comp(const DeBruijnGraph &graph,
                                              DeBruijnGraph::node_index node) {
    std::string seq = graph.get_node_sequence(node);
    std::vector<DeBruijnGraph::node_index> path { node };
    dynamic_cast<const CanonicalDBG&>(graph).reverse_complement(seq, path);
    return path.front();
}


template <typename Graph>
class CanonicalDBGTest : public DeBruijnGraphTest<Graph> { };

// TODO: add support for canonical mode in DBGHashString
typedef ::testing::Types<DBGBitmap,
                         DBGHashOrdered,
                         DBGHashFast,
                         DBGSuccinct,
                         DBGSuccinctBloom<4, 1>,
                         DBGSuccinctCached,
                         DBGSSHash> CanonicalGraphTypes;

TYPED_TEST_SUITE(CanonicalDBGTest, CanonicalGraphTypes);


TYPED_TEST(CanonicalDBGTest, CheckGraph) {
    EXPECT_TRUE(check_graph<TypeParam>("ACGT", DeBruijnGraph::PRIMARY, true));
}

TYPED_TEST(CanonicalDBGTest, CheckGraphInputWithN) {
    EXPECT_TRUE(check_graph<TypeParam>("ACGTN", DeBruijnGraph::PRIMARY, false));
    EXPECT_EQ(TypeParam(3).alphabet().find('N') != std::string::npos,
              check_graph<TypeParam>("ACGTN", DeBruijnGraph::PRIMARY, true));
}

TYPED_TEST(CanonicalDBGTest, InitializeEmpty) {
    auto graph = build_graph<TypeParam>(3, {}, DeBruijnGraph::PRIMARY);

    EXPECT_EQ(0u, graph->num_nodes());
    EXPECT_FALSE(graph->find("AAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    EXPECT_FALSE(graph->find("TTTTTTTTTTTTTTTTTTTTTTTTTTTTT"));
    EXPECT_FALSE(graph->find("CATGTACTAGCTGATCGTAGCTAGCTAGC"));
    EXPECT_FALSE(graph->find("GCTAGCTAGCTACGATCAGCTAGTACATG"));
}

TYPED_TEST(CanonicalDBGTest, InsertSequence) {
    auto graph = build_graph<TypeParam>(21, {
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "CATGTACTAGCTGATCGTAGCTAGCTAGC"
    }, DeBruijnGraph::PRIMARY);

    EXPECT_TRUE(graph->find("AAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    EXPECT_TRUE(graph->find("TTTTTTTTTTTTTTTTTTTTTTTTTTTTT"));
    EXPECT_TRUE(graph->find("CATGTACTAGCTGATCGTAGCTAGCTAGC"));
    EXPECT_TRUE(graph->find("GCTAGCTAGCTACGATCAGCTAGTACATG"));
    EXPECT_FALSE(graph->find("CATGTTTTTTTAATATATATATTTTTAGC"));
    EXPECT_FALSE(graph->find("GCTAAAAATATATATATTAAAAAAACATG"));
}

TYPED_TEST(CanonicalDBGTest, ReverseComplement) {
    auto graph1 = build_graph<TypeParam>(21, { "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA" }, DeBruijnGraph::CANONICAL);
    auto graph2 = build_graph<TypeParam>(21, { "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA" }, DeBruijnGraph::PRIMARY);

    EXPECT_EQ(graph1->num_nodes(), graph2->num_nodes());

    auto graph = build_graph<TypeParam>(21, { "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                              "CATGTACTAGCTGATCGTAGCTAGCTAGC" }, DeBruijnGraph::PRIMARY);
    EXPECT_TRUE(graph->find("AAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    EXPECT_TRUE(graph->find("TTTTTTTTTTTTTTTTTTTTTTTTTTTTT"));
    EXPECT_TRUE(graph->find("CATGTACTAGCTGATCGTAGCTAGCTAGC"));
    EXPECT_TRUE(graph->find("GCTAGCTAGCTACGATCAGCTAGTACATG"));
    EXPECT_FALSE(graph->find("CATGTTTTTTTAATATATATATTTTTAGC"));
    EXPECT_FALSE(graph->find("GCTAAAAATATATATATTAAAAAAACATG"));
}

TYPED_TEST(CanonicalDBGTest, Traversals1) {
    for (size_t k = 2; k < 10; ++k) {
        auto graph = build_graph<TypeParam>(k, {
            std::string(100, 'A') + std::string(100, 'C')
        }, DeBruijnGraph::PRIMARY);

        auto it = DeBruijnGraph::npos;
        auto jt = DeBruijnGraph::npos;
        graph->map_to_nodes_sequentially(std::string(k, 'A'), [&](auto i) { it = i; });
        graph->map_to_nodes_sequentially(std::string(k, 'T'), [&](auto i) {
            jt = i;
            EXPECT_EQ(get_rev_comp(*graph, it), jt);
        });

        auto it2 = DeBruijnGraph::npos;
        auto jt2 = DeBruijnGraph::npos;
        graph->map_to_nodes_sequentially(std::string(k - 1, 'A') + "C", [&](auto i) {
            it2 = i;
        });
        graph->map_to_nodes_sequentially(
            std::string(1, 'G') + std::string(k - 1, 'T'),
            [&](auto i) { jt2 = i; }
        );

        EXPECT_EQ(it, graph->traverse(it, 'A'));
        EXPECT_EQ(it2, graph->traverse(it, 'C'));
        EXPECT_EQ(it, graph->traverse_back(it2, 'A'));

        EXPECT_EQ(jt, graph->traverse(jt, 'T'));
        EXPECT_EQ(jt2, graph->traverse_back(jt, 'G'));
        EXPECT_EQ(jt, graph->traverse(jt2, 'T'));

        EXPECT_EQ(DeBruijnGraph::npos, graph->traverse(it, 'G'));
        EXPECT_EQ(DeBruijnGraph::npos, graph->traverse_back(it2, 'G'));

        graph->map_to_nodes_sequentially(std::string(k, 'G'), [&](auto i) { it = i; });
        ASSERT_NE(DeBruijnGraph::npos, it);
        graph->map_to_nodes_sequentially(std::string(k, 'C'), [&](auto i) {
            jt = i;
            ASSERT_EQ(jt, get_rev_comp(*graph, it));
        });
        graph->map_to_nodes_sequentially(std::string(k - 1, 'G') + "T", [&](auto i) {
            it2 = i;
        });
        ASSERT_NE(DeBruijnGraph::npos, it2);
        EXPECT_EQ(DeBruijnGraph::npos, graph->traverse(it, 'A'));
        EXPECT_EQ(it, graph->traverse(it, 'G'));
        EXPECT_EQ(it2, graph->traverse(it, 'T'));
        EXPECT_EQ(it, graph->traverse_back(it2, 'G'));
    }
}

TYPED_TEST(CanonicalDBGTest, Traversals2) {
    for (size_t k = 2; k < 11; ++k) {
        auto graph = build_graph<TypeParam>(k, {
            std::string(100, 'A') + std::string(100, 'C'),
            std::string(100, 'G') + std::string(100, 'T')
        }, DeBruijnGraph::PRIMARY);

        auto it = DeBruijnGraph::npos;

        graph->map_to_nodes_sequentially(std::string(k, 'A'), [&](auto i) { it = i; });
        ASSERT_NE(DeBruijnGraph::npos, it);

        EXPECT_EQ(it, graph->traverse(it, 'A'));

        ASSERT_NE(DeBruijnGraph::npos, graph->traverse(it, 'C'));
        EXPECT_NE(it, graph->traverse(it, 'C'));

        EXPECT_EQ(it, graph->traverse_back(graph->traverse(it, 'C'), 'A'));

        EXPECT_EQ(DeBruijnGraph::npos, graph->traverse(it, 'G'));
        EXPECT_EQ(DeBruijnGraph::npos, graph->traverse_back(it, 'G'));

        // reverse complement
        graph->map_to_nodes_sequentially(std::string(k, 'G'), [&](auto i) { it = i; });
        ASSERT_NE(DeBruijnGraph::npos, it);

        EXPECT_EQ(it, graph->traverse(it, 'G'));
        ASSERT_EQ(graph->kmer_to_node(std::string(k - 1, 'G') + "T"), graph->traverse(it, 'T'));
        EXPECT_EQ(it, graph->traverse_back(graph->traverse(it, 'T'), 'G'));
    }
}

TYPED_TEST(CanonicalDBGTest, CallPathsEmptyGraphCanonical) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 10; ++k) {
            auto empty = build_graph<TypeParam>(k, {}, DeBruijnGraph::PRIMARY);
            std::vector<std::string> sequences;
            std::mutex seq_mutex;
            empty->call_sequences([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*empty, sequence));
                std::unique_lock<std::mutex> lock(seq_mutex);
                sequences.push_back(sequence);
            }, num_threads);
            ASSERT_EQ(0u, sequences.size());

            EXPECT_EQ(*empty, *build_graph<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY));
            EXPECT_EQ(*empty, *build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY));
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsEmptyGraph) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 10; ++k) {
            auto empty = build_graph<TypeParam>(k, {}, DeBruijnGraph::PRIMARY);
            std::vector<std::string> sequences;
            std::mutex seq_mutex;
            empty->call_unitigs([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*empty, sequence));
                std::unique_lock<std::mutex> lock(seq_mutex);
                sequences.push_back(sequence);
            }, num_threads);
            ASSERT_EQ(0u, sequences.size());

            EXPECT_EQ(*empty, *build_graph<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY));
            EXPECT_EQ(*empty, *build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY));
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallPathsOneSelfLoop) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 20; ++k) {
            std::vector<std::string> sequences { std::string(100, 'A') };
            auto graph = build_graph<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            auto graph_batch = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            ASSERT_EQ(2u, graph->num_nodes());
            ASSERT_EQ(2u, graph_batch->num_nodes());

            std::atomic<size_t> num_sequences = 0;
            graph->call_sequences([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                num_sequences++;
            }, num_threads);
            std::atomic<size_t> num_sequences_batch = 0;
            graph_batch->call_sequences([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph_batch, sequence));
                num_sequences_batch++;
            }, num_threads);

            EXPECT_EQ(graph->num_nodes(), num_sequences);
            EXPECT_EQ(graph_batch->num_nodes(), num_sequences_batch);
            EXPECT_EQ(graph->num_nodes(), graph_batch->num_nodes());
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsOneSelfLoop) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 20; ++k) {
            std::vector<std::string> sequences { std::string(100, 'A') };
            auto graph = build_graph<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            auto graph_batch = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            ASSERT_EQ(2u, graph->num_nodes());
            ASSERT_EQ(2u, graph_batch->num_nodes());

            std::atomic<size_t> num_sequences = 0;
            graph->call_unitigs([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                num_sequences++;
            }, num_threads);
            std::atomic<size_t> num_sequences_batch = 0;
            graph_batch->call_unitigs([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph_batch, sequence));
                num_sequences_batch++;
            }, num_threads);

            EXPECT_EQ(graph->num_nodes(), num_sequences);
            EXPECT_EQ(graph_batch->num_nodes(), num_sequences_batch);
            EXPECT_EQ(graph->num_nodes(), graph_batch->num_nodes());
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallPathsThreeSelfLoops) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 20; ++k) {
            std::vector<std::string> sequences { std::string(100, 'A'),
                                                 std::string(100, 'G'),
                                                 std::string(100, 'C') };
            auto graph = build_graph<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            auto graph_batch = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            ASSERT_EQ(4u, graph->num_nodes());
            ASSERT_EQ(4u, graph_batch->num_nodes());

            std::atomic<size_t> num_sequences = 0;
            graph->call_sequences([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                num_sequences++;
            }, num_threads);
            std::atomic<size_t> num_sequences_batch = 0;
            graph_batch->call_sequences([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph_batch, sequence));
                num_sequences_batch++;
            }, num_threads);

            EXPECT_EQ(graph->num_nodes(), num_sequences);
            EXPECT_EQ(graph_batch->num_nodes(), num_sequences_batch);
            EXPECT_EQ(graph->num_nodes(), graph_batch->num_nodes());
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallPathsExtractsLongestOneLoop) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 7; k < 14; ++k) {
            std::vector<std::string> sequences { "ATGCCGTACTCAG",
                                                 "GGGGGGGGGGGGG" };
            auto graph = build_graph<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);

            std::vector<std::string> contigs;
            std::mutex seq_mutex;
            graph->call_sequences([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                std::unique_lock<std::mutex> lock(seq_mutex);
                contigs.push_back(sequence);
            }, num_threads);

            EXPECT_EQ(4u, contigs.size());
            EXPECT_EQ(convert_to_set({ "ATGCCGTACTCAG", std::string(k, 'G'),
                                       "CTGAGTACGGCAT", std::string(k, 'C') }),
                      convert_to_set(contigs)) << k;
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallContigsUniqueKmers) {
    for (size_t num_threads : { 1, 4 }) {
        std::string sequence = "GCAAATAAC";
        auto graph = build_graph<TypeParam>(3, { sequence }, DeBruijnGraph::PRIMARY);

        std::atomic<size_t> num_kmers = 0;
        graph->call_sequences([&](const auto &sequence, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
            num_kmers += sequence.size() - 2;
        }, num_threads);

        EXPECT_EQ(graph->num_nodes(), num_kmers);
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsUniqueKmersCycle) {
    for (size_t num_threads : { 1, 4 }) {
        size_t k = 5;
        std::string sequence = "AAACCCGGGTTTAAA";
        auto graph = build_graph<TypeParam>(k, { sequence }, DeBruijnGraph::PRIMARY);

        std::atomic<size_t> num_unitigs = 0;
        std::atomic<size_t> num_kmers = 0;
        graph->call_unitigs([&](const auto &sequence, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
            num_unitigs++;
            num_kmers += sequence.size() - k + 1;
        }, num_threads);

        EXPECT_EQ(1u, num_unitigs);
        EXPECT_EQ(graph->num_nodes(), num_kmers);
    }
}

TYPED_TEST(CanonicalDBGTest, CallContigsUniqueKmersCycle) {
    for (size_t num_threads : { 1, 4 }) {
        size_t k = 5;
        std::string sequence = "AAACCCGGGTTTAAA";
        auto graph = build_graph<TypeParam>(k, { sequence }, DeBruijnGraph::PRIMARY);

        std::atomic<size_t> num_contigs = 0;
        std::atomic<size_t> num_kmers = 0;
        graph->call_sequences([&](const auto &sequence, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
            num_contigs++;
            num_kmers += sequence.size() - k + 1;
        }, num_threads);

        EXPECT_EQ(1u, num_contigs) << num_threads;
        EXPECT_EQ(graph->num_nodes(), num_kmers);
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsFourLoops) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 20; ++k) {
            std::vector<std::string> sequences { std::string(100, 'A'),
                                                 std::string(100, 'G'),
                                                 std::string(100, 'C') };
            auto graph = build_graph<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            auto graph_batch = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);
            ASSERT_EQ(4u, graph->num_nodes());
            ASSERT_EQ(4u, graph_batch->num_nodes());

            std::atomic<size_t> num_sequences = 0;
            graph->call_unitigs([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                num_sequences++;
            }, num_threads);
            std::atomic<size_t> num_sequences_batch = 0;
            graph_batch->call_unitigs([&](const auto &sequence, const auto &path) {
                ASSERT_EQ(path, map_to_nodes_sequentially(*graph_batch, sequence));
                num_sequences_batch++;
            }, num_threads);

            EXPECT_EQ(graph->num_nodes(), num_sequences);
            EXPECT_EQ(graph_batch->num_nodes(), num_sequences_batch);
            EXPECT_EQ(graph->num_nodes(), graph_batch->num_nodes());
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallPaths) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 10; ++k) {
            for (const std::vector<std::string> &sequences
                    : { std::vector<std::string>({ "AAACACTAG", "AACGACATG" }),
                        std::vector<std::string>({ "AGACACTGA", "GACTACGTA", "ACTAACGTA" }),
                        std::vector<std::string>({ "AGACACAGT", "GACTTGCAG", "ACTAGTCAG" }),
                        std::vector<std::string>({ "AAACTCGTAGC", "AAATGCGTAGC" }),
                        std::vector<std::string>({ "AAACT", "AAATG" }),
                        std::vector<std::string>({ "ATGCAGTACTCAG", "ATGCAGTAGTCAG", "GGGGGGGGGGGGG" }) }) {

                auto graph = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);

                // in stable graphs the order of input sequences
                // does not change the order of k-mers and their indexes
                auto stable_graph = build_graph_iterative<DBGSuccinct>(k, [&](const auto callback) {
                    for (const auto &seq : sequences) {
                        callback(seq);
                        std::string rev_seq(seq);
                        reverse_complement(rev_seq.begin(), rev_seq.end());
                        callback(rev_seq);
                    }
                });

                std::mutex seq_mutex;
                auto reconstructed_stable_graph = build_graph_iterative<DBGSuccinct>(
                    k,
                    [&](const auto &callback) {
                        graph->call_sequences([&](const auto &sequence, const auto &path) {
                            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                            std::unique_lock<std::mutex> lock(seq_mutex);
                            callback(sequence);
                        }, num_threads);
                    }
                );

                EXPECT_EQ(*stable_graph, *reconstructed_stable_graph);
            }
        }
    }
}

// TODO: A different combination of forward and reverse complement k-mers may be
//       generated in the reconstruction.
// TYPED_TEST(CanonicalDBGTest, CallPathsSingleKmerForm) {
//     for (size_t num_threads : { 1, 4 }) {
//         for (size_t k = 2; k <= 10; ++k) {
//             for (const std::vector<std::string> &sequences
//                     : { std::vector<std::string>({ "AAACACTAG", "AACGACATG" }),
//                         std::vector<std::string>({ "AGACACTGA", "GACTACGTA", "ACTAACGTA" }),
//                         std::vector<std::string>({ "AGACACAGT", "GACTTGCAG", "ACTAGTCAG" }),
//                         std::vector<std::string>({ "AAACTCGTAGC", "AAATGCGTAGC" }),
//                         std::vector<std::string>({ "AAACT", "AAATG" }),
//                         std::vector<std::string>({ "ATGCAGTACTCAG", "ATGCAGTAGTCAG", "GGGGGGGGGGGGG" }) }) {

//                 auto graph = build_graph_batch<TypeParam>(k, sequences, 2);

//                 // in stable graphs the order of input sequences
//                 // does not change the order of k-mers and their indexes
//                 auto stable_graph = build_graph_batch<DBGSuccinct>(k, sequences, false);

//                 std::mutex seq_mutex;
//                 auto reconstructed_stable_graph = build_graph_iterative<DBGSuccinct>(
//                     k,
//                     [&](const auto &callback) {
//                         graph->call_sequences([&](const auto &sequence, const auto &path) {
//                             ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
//                             std::unique_lock<std::mutex> lock(seq_mutex);
//                             callback(sequence);
//                         }, num_threads, true);
//                     },
//                     false
//                 );

//                 EXPECT_EQ(*stable_graph, *reconstructed_stable_graph);
//             }
//         }
//     }
// }

TYPED_TEST(CanonicalDBGTest, CallPathsCheckHalfSingleKmerForm) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 15; ++k) {
            for (const std::vector<std::string> &sequences
                    : { std::vector<std::string>({ "AAACACTAG", "AACGACATG" }),
                        std::vector<std::string>({ "AGACACTGA", "GACTACGTA", "ACTAACGTA" }),
                        std::vector<std::string>({ "AGACACAGT", "GACTTGCAG", "ACTAGTCAG" }),
                        std::vector<std::string>({ "AAACTCGTAGC", "AAATGCGTAGC" }),
                        std::vector<std::string>({ "AAACT", "AAATG" }),
                        std::vector<std::string>({ "ATGCAGTACTCAG", "ATGCAGTAGTCAG", "GGGGGGGGGGGGG" }) }) {

                auto graph = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);

                std::atomic<size_t> num_kmers_both = 0;
                graph->call_sequences([&](const auto &sequence, const auto &path) {
                    EXPECT_EQ(sequence, align::spell_path(*graph, map_to_nodes_sequentially(*graph, sequence)));
                    ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                    num_kmers_both += path.size();
                }, num_threads);

                std::atomic<size_t> num_kmers = 0;
                graph->call_sequences([&](const auto &sequence, const auto &path) {
                    EXPECT_EQ(sequence, align::spell_path(*graph, map_to_nodes_sequentially(*graph, sequence)));
                    ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                    num_kmers += path.size();
                }, num_threads, true);

                if (k % 2) {
                    EXPECT_EQ(num_kmers_both, num_kmers * 2);
                } else {
                    EXPECT_LE(num_kmers_both, num_kmers * 2);
                }
            }
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigs) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 10; ++k) {
            for (const std::vector<std::string> &sequences
                    : { std::vector<std::string>({ "AAACACTAG", "AACGACATG" }),
                        std::vector<std::string>({ "AGACACTGA", "GACTACGTA", "ACTAACGTA" }),
                        std::vector<std::string>({ "AGACACAGT", "GACTTGCAG", "ACTAGTCAG" }),
                        std::vector<std::string>({ "AAACTCGTAGC", "AAATGCGTAGC" }),
                        std::vector<std::string>({ "AAACT", "AAATG" }),
                        std::vector<std::string>({ "ATGCAGTACTCAG", "ATGCAGTAGTCAG", "GGGGGGGGGGGGG" }) }) {

                auto graph = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);

                // in stable graphs the order of input sequences
                // does not change the order of k-mers and their indexes
                auto stable_graph = build_graph_iterative<DBGSuccinct>(k, [&](const auto callback) {
                    for (const auto &seq : sequences) {
                        callback(seq);
                        std::string rev_seq(seq);
                        reverse_complement(rev_seq.begin(), rev_seq.end());
                        callback(rev_seq);
                    }
                });

                std::mutex seq_mutex;
                auto reconstructed_stable_graph = build_graph_iterative<DBGSuccinct>(
                    k,
                    [&](const auto &callback) {
                        graph->call_unitigs([&](const auto &sequence, const auto &path) {
                            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                            std::unique_lock<std::mutex> lock(seq_mutex);
                            callback(sequence);
                        }, num_threads);
                    }
                );

                EXPECT_EQ(*stable_graph, *reconstructed_stable_graph);
            }
        }
    }
}

TYPED_TEST(CanonicalDBGTest, WrapCanonicalGraphFail) {
    ASSERT_DEBUG_DEATH(CanonicalDBG(build_graph_batch<TypeParam>(3, {}, DeBruijnGraph::CANONICAL)), "");
}

// TODO: A different combination of forward and reverse complement k-mers may be
//       generated in the reconstruction.
// TYPED_TEST(CanonicalDBGTest, CallUnitigsSingleKmerForm) {
//     for (size_t num_threads : { 1, 4 }) {
//         for (size_t k = 2; k <= 10; ++k) {
//             for (const std::vector<std::string> &sequences
//                     : { std::vector<std::string>({ "AAACACTAG", "AACGACATG" }),
//                         std::vector<std::string>({ "AGACACTGA", "GACTACGTA", "ACTAACGTA" }),
//                         std::vector<std::string>({ "AGACACAGT", "GACTTGCAG", "ACTAGTCAG" }),
//                         std::vector<std::string>({ "AAACTCGTAGC", "AAATGCGTAGC" }),
//                         std::vector<std::string>({ "AAACT", "AAATG" }),
//                         std::vector<std::string>({ "ATGCAGTACTCAG", "ATGCAGTAGTCAG", "GGGGGGGGGGGGG" }) }) {

//                 auto graph = build_graph_batch<TypeParam>(k, sequences, 2);

//                 // in stable graphs the order of input sequences
//                 // does not change the order of k-mers and their indexes
//                 auto stable_graph = build_graph_iterative<DBGSuccinct>(k, [&](const auto callback) {
//                     for (const auto &seq : sequences) {
//                         callback(seq);
//                         std::string rev_seq(seq);
//                         reverse_complement(rev_seq.begin(), rev_seq.end());
//                         callback(rev_seq);
//                     }
//                 }, false);

//                 std::mutex seq_mutex;
//                 auto reconstructed_stable_graph = build_graph_iterative<DBGSuccinct>(
//                     k,
//                     [&](const auto &callback) {
//                         graph->call_unitigs([&](const auto &sequence, const auto &path) {
//                             ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
//                             std::unique_lock<std::mutex> lock(seq_mutex);
//                             callback(sequence);
//                         }, num_threads, 1, true);
//                     },
//                     false
//                 );

//                 EXPECT_EQ(*stable_graph, *reconstructed_stable_graph);
//             }
//         }
//     }
// }

TYPED_TEST(CanonicalDBGTest, CallUnitigsCheckHalfSingleKmerForm) {
    for (size_t num_threads : { 1, 4 }) {
        for (size_t k = 2; k <= 15; ++k) {
            for (const std::vector<std::string> &sequences
                    : { std::vector<std::string>({ "AAACACTAG", "AACGACATG" }),
                        std::vector<std::string>({ "AGACACTGA", "GACTACGTA", "ACTAACGTA" }),
                        std::vector<std::string>({ "AGACACAGT", "GACTTGCAG", "ACTAGTCAG" }),
                        std::vector<std::string>({ "AAACTCGTAGC", "AAATGCGTAGC" }),
                        std::vector<std::string>({ "AAACT", "AAATG" }),
                        std::vector<std::string>({ "ATGCAGTACTCAG", "ATGCAGTAGTCAG", "GGGGGGGGGGGGG" }) }) {

                auto graph = build_graph_batch<TypeParam>(k, sequences, DeBruijnGraph::PRIMARY);

                std::atomic<size_t> num_kmers_both = 0;
                graph->call_unitigs([&](const auto &sequence, const auto &path) {
                    ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                    num_kmers_both += path.size();
                }, num_threads);

                std::atomic<size_t> num_kmers = 0;
                graph->call_unitigs([&](const auto &sequence, const auto &path) {
                    ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                    num_kmers += path.size();
                }, num_threads, 1, true);

                if (k % 2) {
                    EXPECT_EQ(num_kmers_both, num_kmers * 2);
                } else {
                    EXPECT_LE(num_kmers_both, num_kmers * 2);
                }
            }
        }
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsWithoutTips) {
    for (size_t num_threads : { 1, 4 }) {
        size_t k = 3;
        std::mutex seq_mutex;
        std::set<std::string> unitigs;

        auto graph = build_graph<TypeParam>(k, { "ACTAAGC",
                                                 "TCTAAGC" }, DeBruijnGraph::PRIMARY);
        ASSERT_EQ(12u, graph->num_nodes());

        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 0);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGA", "AGCT", "AGT", "CTA", "CTTA",
                                          "TAAG", "TAG", "TCT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 1);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGA", "AGCT", "AGT", "CTA", "CTTA",
                                          "TAAG", "TAG", "TCT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 2);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGA", "AGCT", "AGT", "CTA", "CTTA",
                                          "TAAG", "TAG", "TCT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 10);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGA", "AGCT", "AGT", "CTA", "CTTA",
                                          "TAAG", "TAG", "TCT" }), unitigs);

        graph = build_graph<TypeParam>(k, { "ACTAAGC",
                                            "ACTAAGT" }, DeBruijnGraph::PRIMARY);
        ASSERT_EQ(10u, graph->num_nodes());

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 0);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGCT", "AGT", "CTA", "CTTA", "TAAG", "TAG" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 1);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGCT", "AGT", "CTA", "CTTA", "TAAG", "TAG" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 2);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGCT", "AGT", "CTA", "CTTA", "TAAG", "TAG" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 10);
        EXPECT_EQ(std::set<std::string>({ "ACT", "AGCT", "AGT", "CTA", "CTTA", "TAAG", "TAG" }), unitigs);

        graph = build_graph<TypeParam>(k, { "ACTAAGCCC",
                                            "AAAGC",
                                            "TAAGCA" }, DeBruijnGraph::PRIMARY);
        ASSERT_EQ(18u, graph->num_nodes());

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 0);
        // EXPECT_EQ(std::set<std::string>({ "ACTAA", "AAA", "AAGC", "GCA", "GCCC" }), unitigs);
        EXPECT_EQ(std::set<std::string>({ "AAA", "AAG", "ACT", "AGC", "AGT", "CCC",
                                          "CTA", "CTT", "GCA", "GCC", "GCT", "GGC",
                                          "GGG", "TAA", "TAG", "TGC", "TTA", "TTT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 1);
        // EXPECT_EQ(std::set<std::string>({ "ACTAA", "AAA", "AAGC", "GCA", "GCCC" }), unitigs);
        EXPECT_EQ(std::set<std::string>({ "AAA", "AAG", "ACT", "AGC", "AGT", "CCC",
                                          "CTA", "CTT", "GCA", "GCC", "GCT", "GGC",
                                          "GGG", "TAA", "TAG", "TGC", "TTA", "TTT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 2);
        // EXPECT_EQ(std::set<std::string>({ "ACTAA", "AAGC", "GCC", "CCC" }), unitigs);
        EXPECT_EQ(std::set<std::string>({ "AAA", "AAG", "ACT", "AGC", "AGT", "CCC",
                                          "CTA", "CTT", "GCA", "GCC", "GCT", "GGC",
                                          "GGG", "TAA", "TAG", "TGC", "TTA", "TTT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 3);
        // EXPECT_EQ(std::set<std::string>({ "ACTAA", "AAGC" }), unitigs);
        EXPECT_EQ(std::set<std::string>({ "AAA", "AAG", "ACT", "AGC", "AGT", "CCC",
                                          "CTA", "CTT", "GCA", "GCC", "GCT", "GGC",
                                          "GGG", "TAA", "TAG", "TGC", "TTA", "TTT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 10);
        // EXPECT_EQ(std::set<std::string>({ "AAGC" }), unitigs);
        EXPECT_EQ(std::set<std::string>({ "AAA", "AAG", "ACT", "AGC", "AGT", "CCC",
                                          "CTA", "CTT", "GCA", "GCC", "GCT", "GGC",
                                          "GGG", "TAA", "TAG", "TGC", "TTA", "TTT" }), unitigs);

        graph = build_graph<TypeParam>(k, { "ACGAAGCCT",
                                            "AAGC",
                                            "TAAGCA" }, DeBruijnGraph::PRIMARY);
        ASSERT_EQ(18u, graph->num_nodes());

        // TODO: make DBGSuccinct work properly even if it has redundant source dummy edges
        if (dynamic_cast<DBGSuccinct*>(graph.get()))
            dynamic_cast<DBGSuccinct&>(*graph).mask_dummy_kmers(1, true);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 0);
        EXPECT_EQ(std::set<std::string>({ "AAG", "ACG", "AGC", "AGGC", "CGAA",
                                          "CGT", "CTT", "GCA", "GCCT", "GCT",
                                          "TGC", "TTAA", "TTCG" }), unitigs) << *graph;

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 1);
        EXPECT_EQ(std::set<std::string>({ "AAG", "ACG", "AGC", "AGGC", "CGAA",
                                          "CGT", "CTT", "GCA", "GCCT", "GCT",
                                          "TGC", "TTAA", "TTCG" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 2);
        EXPECT_EQ(std::set<std::string>({ "AAG", "ACG", "AGC", "AGGC", "CGAA",
                                          "CGT", "CTT", "GCA", "GCCT", "GCT",
                                          "TGC", "TTAA", "TTCG" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 3);
        EXPECT_EQ(std::set<std::string>({ "AAG", "ACG", "AGC", "AGGC", "CGAA",
                                          "CGT", "CTT", "GCA", "GCCT", "GCT",
                                          "TGC", "TTAA", "TTCG" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 10);
        EXPECT_EQ(std::set<std::string>({ "AAG", "ACG", "AGC", "AGGC", "CGAA",
                                          "CGT", "CTT", "GCA", "GCCT", "GCT",
                                          "TGC", "TTAA", "TTCG" }), unitigs);

        graph = build_graph<TypeParam>(k, { "TCTAAGCCG",
                                            "CATAAGCCG",
                                            "CATAACCGA" }, DeBruijnGraph::PRIMARY);
        ASSERT_EQ(24u, graph->num_nodes());

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 0);
        EXPECT_EQ(std::set<std::string>({ "AACC", "AAG", "AGA", "AGC", "ATA", "ATG",
                                          "CAT", "CCG", "CGA", "CGG", "CTA", "CTT",
                                          "GCC", "GCT", "GGC", "GGTT", "TAA", "TAG",
                                          "TAT", "TCG", "TCT", "TTA" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 1);
        EXPECT_EQ(std::set<std::string>({ "AACC", "AAG", "AGA", "AGC", "ATA", "ATG",
                                          "CAT", "CCG", "CGA", "CGG", "CTA", "CTT",
                                          "GCC", "GCT", "GGC", "GGTT", "TAA", "TAG",
                                          "TAT", "TCG", "TCT", "TTA" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 2);
        EXPECT_EQ(std::set<std::string>({ "AACC", "AAG", "AGA", "AGC", "ATA", "ATG",
                                          "CAT", "CCG", "CGA", "CGG", "CTA", "CTT",
                                          "GCC", "GCT", "GGC", "GGTT", "TAA", "TAG",
                                          "TAT", "TCG", "TCT", "TTA" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 3);
        EXPECT_EQ(std::set<std::string>({ "AACC", "AAG", "AGA", "AGC", "ATA", "ATG",
                                          "CAT", "CCG", "CGA", "CGG", "CTA", "CTT",
                                          "GCC", "GCT", "GGC", "GGTT", "TAA", "TAG",
                                          "TAT", "TCG", "TCT", "TTA" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 10);
        EXPECT_EQ(std::set<std::string>({ "AACC", "AAG", "AGA", "AGC", "ATA", "ATG",
                                          "CAT", "CCG", "CGA", "CGG", "CTA", "CTT",
                                          "GCC", "GCT", "GGC", "GGTT", "TAA", "TAG",
                                          "TAT", "TCG", "TCT", "TTA" }), unitigs);
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsWithoutTips2) {
    for (size_t num_threads : { 1, 4 }) {
        size_t k = 5;
        auto graph = build_graph<TypeParam>(k, { "ACTATAGCTAGTCTATGCGA",
                                                 "ACTATAGCTAGTCTAA",
                                                 "ACTATAGCTA",
                                                 "ACTATAGCTT",
                                                 "ACTATC", }, DeBruijnGraph::PRIMARY);
        std::mutex seq_mutex;
        ASSERT_EQ(34u, graph->num_nodes());
        std::set<std::string> unitigs;
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 0);
        EXPECT_EQ(std::set<std::string>({ "AAGCT", "ACTAG", "ACTAT", "AGCTA", "AGCTT",
                                          "ATAGA", "ATAGC", "ATAGT", "CTAGC", "CTAGT",
                                          "CTATAG", "CTATC", "CTATGCGA", "GATAG",
                                          "GCTAG", "GCTAT", "TAGACTA", "TAGCT",
                                          "TAGTCTA", "TCGCATAG", "TCTAA", "TCTAT",
                                          "TTAGA" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 1);
        EXPECT_EQ(std::set<std::string>({ "AAGCT", "ACTAG", "ACTAT", "AGCTA", "AGCTT",
                                          "ATAGA", "ATAGC", "ATAGT", "CTAGC", "CTAGT",
                                          "CTATAG", "CTATC", "CTATGCGA", "GATAG",
                                          "GCTAG", "GCTAT", "TAGACTA", "TAGCT",
                                          "TAGTCTA", "TCGCATAG", "TCTAA", "TCTAT",
                                          "TTAGA" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 2);
        EXPECT_EQ(std::set<std::string>({ "AAGCT", "ACTAG", "ACTAT", "AGCTA", "AGCTT",
                                          "ATAGA", "ATAGC", "ATAGT", "CTAGC", "CTAGT",
                                          "CTATAG", "CTATC", "CTATGCGA", "GATAG",
                                          "GCTAG", "GCTAT", "TAGACTA", "TAGCT",
                                          "TAGTCTA", "TCGCATAG", "TCTAT" }), unitigs);

        unitigs.clear();
        graph->call_unitigs([&](const auto &unitig, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, unitig));
            std::unique_lock<std::mutex> lock(seq_mutex);
            unitigs.insert(unitig);
        }, num_threads, 10);
        EXPECT_EQ(std::set<std::string>({ "AAGCT", "ACTAG", "ACTAT", "AGCTA", "AGCTT",
                                          "ATAGA", "ATAGC", "ATAGT", "CTAGC", "CTAGT",
                                          "CTATAG", "CTATC", "CTATGCGA", "GATAG",
                                          "GCTAG", "GCTAT", "TAGACTA", "TAGCT",
                                          "TAGTCTA", "TCGCATAG", "TCTAT" }), unitigs);
    }
}

TYPED_TEST(CanonicalDBGTest, CallKmersEmptyGraph) {
    for (size_t k = 2; k <= 30; ++k) {
        auto empty = build_graph<TypeParam>(k, {}, DeBruijnGraph::PRIMARY);
        size_t num_kmers = 0;
        empty->call_kmers([&](auto, const auto &sequence) {
            EXPECT_FALSE(true) << sequence;
            num_kmers++;
        });

        EXPECT_EQ(0u, num_kmers);
    }
}

TYPED_TEST(CanonicalDBGTest, CallKmersTwoLoops) {
    for (size_t k = 2; k <= 20; ++k) {
        auto graph = build_graph<TypeParam>(k, { std::string(100, 'A') }, DeBruijnGraph::PRIMARY);

        ASSERT_EQ(2u, graph->num_nodes());

        size_t num_kmers = 0;
        graph->call_kmers([&](auto, const auto &sequence) {
            EXPECT_TRUE(std::string(k, 'A') == sequence
                        || std::string(k, 'T') == sequence);
            num_kmers++;
        });
        EXPECT_EQ(2u, num_kmers);
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsCheckDegree) {
    for (size_t num_threads : { 1, 4 }) {
        std::vector<std::string> sequences {
            "CCAGGGTGTGCTTGTCAAAGAGATATTCCGCCAAGCCAGATTCGGGCGG",
            "CCAGGGTGTGCTTGTCAAAGAGATATTCCGCCAAGCCAGATTCGGGCGC",
            "CCAAAATGAAACCTTCAGTTTTAACTCTTAATCAGACATAACTGGAAAA",
            "CCGAACTAGTGAAACTGCAACAGACATACGCTGCTCTGAACTCTAAGGC",
            "CCAGGTGCAGGGTGGACTCTTTCTGGATGTTGTAGTCAGACAGGGTGCG",
            "ATCGGAAGAGCACACGTCTGAACTCCAGACACTAAGGCATCTCGTATGC",
            "CGGAGGGAAAAATATTTACACAGAGTAGGAGACAAATTGGCTGAAAAGC",
            "CCAGAGTCTCGTTCGTTATCGGAATTAACCAGACAAATCGCTCCACCAA"
        };

        auto graph = build_graph_batch<TypeParam>(9, sequences, DeBruijnGraph::PRIMARY);

        std::multiset<std::string> unitigs {
            "AAATATTTACACAGAGTAGGAGACAAAT",
            "AAATATTTTTCCCTCCG",
            "AGACAAATCGCTCCACCAA",
            "AGACAAATTGGCTGAAAAGC",
            "AGTTCAGACGTGTGCTCTTCCGAT",
            "AGTTCAGAGCAGCGTATGTCTG",
            "ATCGGAAGAGCACACGTCTGAACT",
            "ATTTGTCTCCTACTCTGTGTAAATATTT",
            "ATTTGTCTGGTTAATTCCGATAACGAACGAGACTCTGG",
            "CAGACATAACTGGAAAA",
            "CAGACATACGCTGCTCTGAACT",
            "CCAAAATGAAACCTTCAGTTTTAACTCTTAATCAGACATA",
            "CCAGAGTCTCGTTCGTTATCGGAATTAACCAGACAAAT",
            "CCAGGGTGTGCTTGTCAAAGAGATATTCCGCCAAGCCAGATTCGGGCG",
            "CCAGGTGCAGGGTGGACTCTTTCTGGATGTTGTAGTCAGACAGGGTGCG",
            "CCGAACTAGTGAAACTGCAACAGACATA",
            "CGCACCCTGTCTGACTACAACATCCAGAAAGAGTCCACCCTGCACCTGG",
            "CGCCCGAATCTGGCTTGGCGGAATATCTCTTTGACAAGCACACCCTGG",
            "CGGAGGGAAAAATATTT",
            "CTGAACTCCAGACACTAAGGCATCTCGTATGC",
            "CTGAACTCTAAGGC",
            "GAGTTCAGA",
            "GCATACGAGATGCCTTAGTGTCTGGAGTTCAG",
            "GCCTTAGAGTTCAG",
            "GCTTTTCAGCCAATTTGTCT",
            "TATGTCTGATTAAGAGTTAAAACTGAAGGTTTCATTTTGG",
            "TATGTCTGTTGCAGTTTCACTAGTTCGG",
            "TCTGAACTC",
            "TTGGTGGAGCGATTTGTCT",
            "TTTTCCAGTTATGTCTG"
        };

        std::mutex seq_mutex;
        std::multiset<std::string> obs_unitigs;
        graph->call_unitigs([&](const auto &sequence, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
            std::unique_lock<std::mutex> lock(seq_mutex);
            obs_unitigs.insert(sequence);
        }, num_threads, 2);

        EXPECT_EQ(unitigs, obs_unitigs);
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsIndegreeFirstNodeIsZero) {
    for (size_t num_threads : { 1, 4 }) {
        std::vector<std::string> sequences {
            "AGAAACCCCGTCTCTACTAAAAATACAAAATTAGCCGGGAGTGGTGGCG",
            "AGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCAGGTGTGGTGAC",
            "GCCTGACCAGCATGGTGAAACCCCGTCTCTACTAAAAATACAAAATTAG"
        };

        auto graph = build_graph_batch<TypeParam>(31, sequences, DeBruijnGraph::PRIMARY);

        std::multiset<std::string> unitigs {
            "AGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCAGGTGTGGTGAC",
            "ATTTTGTATTTTTAGTAGAGACGGGGTTTCACCATGCTGGTCAGGC",
            "CGCCACCACTCCCGGCTAATTTTGTATTTTTAGTAGAGACGGGGTTTC",
            "GAAACCCCGTCTCTACTAAAAATACAAAATTAGCCGGGAGTGGTGGCG",
            "GCCTGACCAGCATGGTGAAACCCCGTCTCTACTAAAAATACAAAAT",
            "GTCACCACACCTGGCTAATTTTTGTATTTTTAGTAGAGACGGGGTTTCT"
        };

        std::multiset<std::string> obs_unitigs;
        std::mutex seq_mutex;
        graph->call_unitigs([&](const auto &sequence, const auto &path) {
            ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
            std::unique_lock<std::mutex> lock(seq_mutex);
            obs_unitigs.insert(sequence);
        }, num_threads, 2);

        EXPECT_EQ(unitigs, obs_unitigs);
    }
}

TYPED_TEST(CanonicalDBGTest, CallUnitigsCross) {
    for (size_t num_threads : { 1, 4 }) {
        // AATTT - ATTTT           TTTAA - TTAAA
        //               > TTTTA <
        // GGTTT - GTTTT           TTTAG - TTAGG

        // build graph from k-mers added in different order
        std::mutex seq_mutex;
        for (const auto &sequences : {
            std::vector<std::string>({ "AATTTTAAA",
                                       "GGTTTTAGG", }),
            std::vector<std::string>({ "GGTTTTAGG",
                                       "AATTTTAAA", }),
            std::vector<std::string>({ "TTTTAAA",
                                       "TTTTAGG",
                                       "AATTTTA",
                                       "GGTTTTA", }),
            std::vector<std::string>({ "AATTTTA",
                                       "GGTTTTA",
                                       "TTTTAAA",
                                       "TTTTAGG", }) }) {
            auto graph = build_graph_batch<TypeParam>(5, sequences, DeBruijnGraph::PRIMARY);

            std::multiset<std::string> unitigs {
                "AAAACC",
                "AAAATTTT",
                "CCTAAA",
                "GGTTTT",
                "TAAAA",
                "TTTAAA",
                "TTTAGG",
                "TTTTA",
            };

            for (size_t t = 0; t <= 2; ++t) {
                std::multiset<std::string> obs_unitigs;
                graph->call_unitigs([&](const auto &sequence, const auto &path) {
                    ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                    std::unique_lock<std::mutex> lock(seq_mutex);
                    obs_unitigs.insert(sequence);
                }, num_threads, t);
                EXPECT_EQ(unitigs, obs_unitigs) << t;
            }

            std::multiset<std::string> long_unitigs {
                "AAAATTTT",
                "TAAAA",
                "TTTAAA",
                "TTTTA",
            };

            for (size_t t = 3; t <= 10; ++t) {
                std::multiset<std::string> obs_long_unitigs;
                graph->call_unitigs([&](const auto &sequence, const auto &path) {
                    ASSERT_EQ(path, map_to_nodes_sequentially(*graph, sequence));
                    std::unique_lock<std::mutex> lock(seq_mutex);
                    obs_long_unitigs.insert(sequence);
                }, num_threads, 3);
                EXPECT_EQ(long_unitigs, obs_long_unitigs);
            }
        }
    }
}

TYPED_TEST(CanonicalDBGTest, TraversalDummySink) {
    if (!std::is_base_of_v<DBGSuccinct, TypeParam>) {
        common::logger->warn("Dummy k-mers only supported for DBGSuccinct");
        return;
    }

    std::vector<std::string> sequences { "CTTCCTTCCTTCTTTCCTTCCTTCCTTCCTC" };

    auto graph = std::make_shared<DBGSuccinct>(31, DeBruijnGraph::PRIMARY);
    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence);
    }

    DBGSuccinct &dbg_succ = *dynamic_cast<DBGSuccinct*>(graph.get());

    if (std::is_same_v<TypeParam, DBGSuccinctCached>)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(dbg_succ));

    CanonicalDBG canonical(graph);
    std::string query = "CTTCCTTCCTTCTTTCCTTCCTTCCTTCCTC";
    auto node = map_to_nodes_sequentially(canonical, query)[0];
    ASSERT_NE(0u, node);

    size_t num_found = 0;

    graph->call_outgoing_kmers(node, [&](auto, char c) {
        ASSERT_EQ(boss::BOSS::kSentinel, c);
        ++num_found;
    });
    ASSERT_EQ(1u, num_found);

    num_found = 0;
    graph->adjacent_outgoing_nodes(node, [&](auto next) {
        ASSERT_EQ(boss::BOSS::kSentinel, graph->get_node_sequence(next).back());
        ++num_found;
    });
    ASSERT_EQ(1u, num_found);

    num_found = 0;
    canonical.call_outgoing_kmers(node, [&](auto, char c) {
        EXPECT_EQ(boss::BOSS::kSentinel, c);
        ++num_found;
    });
    EXPECT_EQ(1u, num_found);

    num_found = 0;
    canonical.adjacent_outgoing_nodes(node, [&](auto next) {
        EXPECT_EQ(boss::BOSS::kSentinel, canonical.get_node_sequence(next).back());
        ++num_found;
    });
    EXPECT_EQ(1u, num_found);
}

TYPED_TEST(CanonicalDBGTest, TraversalDummySource) {
    if (!std::is_base_of_v<DBGSuccinct, TypeParam>) {
        common::logger->warn("Dummy k-mers only supported for DBGSuccinct");
        return;
    }

    std::vector<std::string> sequences { "CTTCCTTCCTTCTTTCCTTCCTTCCTTCCTC" };

    auto graph = std::make_shared<DBGSuccinct>(31, DeBruijnGraph::PRIMARY);
    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence);
    }

    DBGSuccinct &dbg_succ = *dynamic_cast<DBGSuccinct*>(graph.get());

    if (std::is_same_v<TypeParam, DBGSuccinctCached>)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(dbg_succ));

    CanonicalDBG canonical(graph);
    std::string query = "CTTCCTTCCTTCTTTCCTTCCTTCCTTCCTC";
    auto node = map_to_nodes_sequentially(canonical, query)[0];
    ASSERT_NE(0u, node);

    size_t num_found = 0;

    graph->call_incoming_kmers(node, [&](auto, char c) {
        ASSERT_EQ(boss::BOSS::kSentinel, c);
        ++num_found;
    });
    ASSERT_EQ(1u, num_found);

    num_found = 0;
    graph->adjacent_incoming_nodes(node, [&](auto prev) {
        ASSERT_EQ(boss::BOSS::kSentinel, graph->get_node_sequence(prev).front());
        ++num_found;
    });
    ASSERT_EQ(1u, num_found);

    num_found = 0;
    canonical.call_incoming_kmers(node, [&](auto, char c) {
        EXPECT_EQ(boss::BOSS::kSentinel, c);
        ++num_found;
    });

    EXPECT_EQ(1u, num_found);

    num_found = 0;
    canonical.adjacent_incoming_nodes(node, [&](auto prev) {
        EXPECT_EQ(boss::BOSS::kSentinel, canonical.get_node_sequence(prev).front());
        ++num_found;
    });
    EXPECT_EQ(1u, num_found);
}

TYPED_TEST(CanonicalDBGTest, TraversalDummy) {
    if (!std::is_base_of_v<DBGSuccinct, TypeParam>) {
        common::logger->warn("Dummy k-mers only supported for DBGSuccinct");
        return;
    }

    std::vector<std::string> sequences {
        "CTTCCTTCCTTCTTTCCTTCCTTCCTTCCTC",
        "AAGGAAGGAAGGAAGGAAAGAAGGAAGGAAG"
    };

    auto graph = std::make_shared<DBGSuccinct>(31, DeBruijnGraph::PRIMARY);
    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence);
    }

    DBGSuccinct &dbg_succ = *dynamic_cast<DBGSuccinct*>(graph.get());

    if (std::is_same_v<TypeParam, DBGSuccinctCached>)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(dbg_succ));

    CanonicalDBG canonical(graph);
    const auto &boss = dbg_succ.get_boss();

    std::string query = "CTTCCTTCCTTCTTTCCTTCCTTCCTTCCT";
    auto encoded = boss.encode(query);
    auto [edge_1, edge_2, end] = boss.index_range(encoded.begin(), encoded.end());
    ASSERT_EQ(end, encoded.end());
    edge_2 = boss.bwd(edge_2);

    ASSERT_EQ(std::string(1, '$') + query, canonical.get_node_sequence(edge_2));

    size_t num_real_edges = 0;
    graph->call_outgoing_kmers(edge_2, [&](auto, char c) {
        EXPECT_EQ('C', c);
        ++num_real_edges;
    });
    EXPECT_EQ(1u, num_real_edges);

    std::vector<char> out_edges;
    canonical.call_outgoing_kmers(edge_2, [&](auto, char c) {
        out_edges.emplace_back(c);
    });

    ASSERT_EQ(2u, out_edges.size());
    if (out_edges[0] > out_edges[1])
        std::swap(out_edges[0], out_edges[1]);

    EXPECT_EQ(std::vector<char>({ 'C', 'T' }), out_edges);
}

TYPED_TEST(CanonicalDBGTest, TraverseNoDummyToDummyForward) {
    if (!std::is_base_of_v<DBGSuccinct, TypeParam>) {
        common::logger->warn("Dummy k-mers only supported for DBGSuccinct");
        return;
    }

    // meant to make sure that the following traversal doesn't happen
    // $TGTGCGGCGGGAATATGTACGAAGCGCAGG -> rc(TGTGCGGCGGGAATATGTACGAAGCGCAGG$)

    std::vector<std::string> sequences {
        "TGTGCGGCGGGAATATGTACGAAGCGCAGGA",
        "CCTGCGCTTCGTACATATTCCCGCCGCACAG"
    };

    auto graph = std::make_shared<DBGSuccinct>(31, DeBruijnGraph::PRIMARY);
    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence);
    }

    DBGSuccinct &dbg_succ = *dynamic_cast<DBGSuccinct*>(graph.get());

    if (std::is_same_v<TypeParam, DBGSuccinctCached>)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(dbg_succ));

    CanonicalDBG canonical(graph);
    const auto &boss = dbg_succ.get_boss();

    std::string query = "TGTGCGGCGGGAATATGTACGAAGCGCAGG";

    auto encoded = boss.encode(query);
    auto [edge_1, edge_2, end] = boss.index_range(encoded.begin(), encoded.end());
    ASSERT_EQ(end, encoded.end());
    edge_2 = boss.bwd(edge_2);

    ASSERT_EQ(std::string(1, '$') + query, canonical.get_node_sequence(edge_2));
    bool found_edge = false;
    graph->call_outgoing_kmers(edge_2, [&](auto, char c) {
        found_edge = true;
        EXPECT_EQ('A', c);
    });
    EXPECT_TRUE(found_edge);

    canonical.call_outgoing_kmers(edge_2, [&](auto next, char c) {
        EXPECT_NE(boss::BOSS::kSentinel, c);
    });

    canonical.adjacent_outgoing_nodes(edge_2, [&](auto next) {
        char c = canonical.get_node_sequence(next).back();
        EXPECT_NE(boss::BOSS::kSentinel, c);
    });
}

TYPED_TEST(CanonicalDBGTest, TraverseNoDummyToDummyBackward) {
    if (!std::is_base_of_v<DBGSuccinct, TypeParam>) {
        common::logger->warn("Dummy k-mers only supported for DBGSuccinct");
        return;
    }

    // meant to make sure that the following traversal doesn't happen
    // $TCCTGCGCTTCGTACATATTCCCGCCGCAC         -> TCCTGCGCTTCGTACATATTCCCGCCGCAC$
    // (r.c. GTGCGGCGGGAATATGTACGAAGCGCAGGA$)
    std::vector<std::string> sequences {
        "TGTGCGGCGGGAATATGTACGAAGCGCAGGA",
        "TTCCTGCGCTTCGTACATATTCCCGCCGCAC",
    };

    auto graph = std::make_shared<DBGSuccinct>(31, DeBruijnGraph::PRIMARY);
    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence);
    }

    DBGSuccinct &dbg_succ = *dynamic_cast<DBGSuccinct*>(graph.get());

    if (std::is_same_v<TypeParam, DBGSuccinctCached>)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(dbg_succ));

    CanonicalDBG canonical(graph);
    const auto &boss = dbg_succ.get_boss();

    std::string query = "GTGCGGCGGGAATATGTACGAAGCGCAGGA";
    auto encoded = boss.encode(query);
    auto [edge_1, edge_2, end] = boss.index_range(encoded.begin(), encoded.end());
    ASSERT_EQ(end, encoded.end());
    ASSERT_EQ(edge_1, edge_2);
    ASSERT_EQ(boss::BOSS::kSentinelCode, boss.get_W(edge_2));

    auto node = canonical.reverse_complement(edge_2);
    bool found = false;
    canonical.call_outgoing_kmers(node, [&](auto, char c) {
        found = true;
        EXPECT_EQ('A', c);
    });
    ASSERT_TRUE(found);

    found = false;
    canonical.adjacent_outgoing_nodes(node, [&](auto next) {
        found = true;
        char c = canonical.get_node_sequence(next).back();
        EXPECT_EQ('A', c);
    });
    EXPECT_TRUE(found);
}

TYPED_TEST(CanonicalDBGTest, TraverseNoDualDummy) {
    if (!std::is_base_of_v<DBGSuccinct, TypeParam>) {
        common::logger->warn("Dummy k-mers only supported for DBGSuccinct");
        return;
    }

    // meant to make sure that the reverse complements of dummy k-mers are not present
    // $TCCTGCGCTTCGTACATATTCCCGCCGCAC and GTGCGGCGGGAATATGTACGAAGCGCAGGA$
    std::vector<std::string> sequences {
        "TCCTGCGCTTCGTACATATTCCCGCCGCACT",
        "TGTGCGGCGGGAATATGTACGAAGCGCAGGA",
    };

    auto graph = std::make_shared<DBGSuccinct>(31, DeBruijnGraph::PRIMARY);
    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence);
    }

    DBGSuccinct &dbg_succ = *dynamic_cast<DBGSuccinct*>(graph.get());

    if (std::is_same_v<TypeParam, DBGSuccinctCached>)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(dbg_succ));

    CanonicalDBG canonical(graph);
    const auto &boss = dbg_succ.get_boss();

    {
        std::string query = "TGTGCGGCGGGAATATGTACGAAGCGCAGG";
        auto encoded = boss.encode(query);
        auto [edge_1, edge_2, end] = boss.index_range(encoded.begin(), encoded.end());
        ASSERT_EQ(end, encoded.end());
        ASSERT_EQ(edge_1, edge_2);
        ASSERT_EQ('A', boss.decode(boss.get_W(edge_2)));

        bool found_dummy = false;
        ASSERT_NO_THROW(canonical.call_outgoing_kmers(edge_2, [&](auto, char c) {
            EXPECT_FALSE(found_dummy);
            found_dummy |= (boss::BOSS::kSentinel == c);
        }));
        EXPECT_TRUE(found_dummy);

        found_dummy = false;
        ASSERT_NO_THROW(canonical.adjacent_outgoing_nodes(edge_2, [&](auto next) {
            EXPECT_FALSE(found_dummy);
            found_dummy |= (canonical.get_node_sequence(next).back() == boss::BOSS::kSentinel);
        }));
        EXPECT_TRUE(found_dummy);
    }
    {
        std::string query = "TCCTGCGCTTCGTACATATTCCCGCCGCAC";
        auto encoded = boss.encode(query);
        auto [edge_1, edge_2, end] = boss.index_range(encoded.begin(), encoded.end());
        ASSERT_EQ(end, encoded.end());
        ASSERT_EQ(edge_1, edge_2);
        ASSERT_EQ('T', boss.decode(boss.get_W(edge_2)));

        bool found_dummy = false;
        ASSERT_NO_THROW(canonical.call_incoming_kmers(edge_2, [&](auto, char c) {
            EXPECT_FALSE(found_dummy);
            found_dummy |= (boss::BOSS::kSentinel == c);
        }));
        EXPECT_TRUE(found_dummy);

        found_dummy = false;
        ASSERT_NO_THROW(canonical.adjacent_incoming_nodes(edge_2, [&](auto next) {
            EXPECT_FALSE(found_dummy);
            found_dummy |= (canonical.get_node_sequence(next).front() == boss::BOSS::kSentinel);
        }));
        EXPECT_TRUE(found_dummy);
    }
}

TYPED_TEST(CanonicalDBGTest, TraverseNoDuplicateDummySink) {
    if (!std::is_base_of_v<DBGSuccinct, TypeParam>) {
        common::logger->warn("Dummy k-mers only supported for DBGSuccinct");
        return;
    }

    // meant to make sure that the reverse complements of dummy k-mers are not present
    // CCTGCGCTTCGTACATATTCCCGCCGCACT$ and $AGTGCGGCGGGAATATGTACGAAGCGCAGG
    std::vector<std::string> sequences {
        "TCCTGCGCTTCGTACATATTCCCGCCGCACT",
        "AGTGCGGCGGGAATATGTACGAAGCGCAGGC",
    };

    auto graph = std::make_shared<DBGSuccinct>(31, DeBruijnGraph::PRIMARY);
    for (const auto &sequence : sequences) {
        graph->add_sequence(sequence);
    }

    DBGSuccinct &dbg_succ = *dynamic_cast<DBGSuccinct*>(graph.get());

    if (std::is_same_v<TypeParam, DBGSuccinctCached>)
        graph->add_extension(std::make_shared<graph::NodeFirstCache>(dbg_succ));

    CanonicalDBG canonical(graph);

    {
        std::string query = "TCCTGCGCTTCGTACATATTCCCGCCGCACT";
        DeBruijnGraph::node_index start_node = map_to_nodes_sequentially(canonical, query)[0];
        DeBruijnGraph::node_index last_node = 0;
        canonical.call_outgoing_kmers(start_node, [&](auto node, char c) {
            ASSERT_EQ(boss::BOSS::kSentinel, c);
            EXPECT_NE(last_node, node);
            last_node = node;
        });

        last_node = 0;
        canonical.adjacent_outgoing_nodes(start_node, [&](auto node) {
            EXPECT_NE(last_node, node);
            last_node = node;
        });
    }
    {
        std::string query = "AGTGCGGCGGGAATATGTACGAAGCGCAGGC";
        DeBruijnGraph::node_index start_node = map_to_nodes_sequentially(canonical, query)[0];
        DeBruijnGraph::node_index last_node = 0;
        canonical.call_incoming_kmers(start_node, [&](auto node, char c) {
            ASSERT_EQ(boss::BOSS::kSentinel, c);
            EXPECT_NE(last_node, node);
            last_node = node;
        });

        last_node = 0;
        canonical.adjacent_incoming_nodes(start_node, [&](auto node) {
            EXPECT_NE(last_node, node);
            last_node = node;
        });
    }
}

#endif // ! _PROTEIN_GRAPH

} // namespace
