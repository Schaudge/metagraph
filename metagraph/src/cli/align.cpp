#include "align.hpp"

#include "common/logger.hpp"
#include "common/algorithms.hpp"
#include "common/unix_tools.hpp"
#include "common/threads/threading.hpp"
#include "graph/representation/succinct/dbg_succinct.hpp"
#include "graph/alignment/dbg_aligner.hpp"
#include "graph/alignment/aligner_methods.hpp"
#include "config/config.hpp"
#include "load/load_graph.hpp"
#include "parse_sequences.hpp"

using mg::common::logger;


DBGAlignerConfig initialize_aligner_config(const DeBruijnGraph &graph, Config &config) {
    // fix seed length bounds
    if (!config.alignment_min_seed_length || config.alignment_seed_unimems)
        config.alignment_min_seed_length = graph.get_k();

    if (config.alignment_max_seed_length == std::numeric_limits<size_t>::max()
            && !config.alignment_seed_unimems)
        config.alignment_max_seed_length = graph.get_k();

    DBGAlignerConfig aligner_config;

    aligner_config.queue_size = config.alignment_queue_size;
    aligner_config.bandwidth = config.alignment_vertical_bandwidth;
    aligner_config.num_alternative_paths = config.alignment_num_alternative_paths;
    aligner_config.min_seed_length = config.alignment_min_seed_length;
    aligner_config.max_seed_length = config.alignment_max_seed_length;
    aligner_config.max_num_seeds_per_locus = config.alignment_max_num_seeds_per_locus;
    aligner_config.min_cell_score = config.alignment_min_cell_score;
    aligner_config.min_path_score = config.alignment_min_path_score;
    aligner_config.gap_opening_penalty = -config.alignment_gap_opening_penalty;
    aligner_config.gap_extension_penalty = -config.alignment_gap_extension_penalty;
    aligner_config.forward_and_reverse_complement = config.forward_and_reverse;
    aligner_config.alignment_edit_distance = config.alignment_edit_distance;
    aligner_config.alignment_match_score = config.alignment_match_score;
    aligner_config.alignment_mm_transition_score = config.alignment_mm_transition_score;
    aligner_config.alignment_mm_transversion_score = config.alignment_mm_transversion_score;

    logger->trace("Alignment settings:");
    logger->trace("\t Seeding: {}", (config.alignment_seed_unimems ? "unimems" : "nodes"));
    logger->trace("\t Alignments to report: {}", config.alignment_num_alternative_paths);
    logger->trace("\t Priority queue size: {}", config.alignment_queue_size);
    logger->trace("\t Min seed length: {}", config.alignment_min_seed_length);
    logger->trace("\t Max seed length: {}", config.alignment_max_seed_length);
    logger->trace("\t Max num seeds per locus: {}", config.alignment_max_num_seeds_per_locus);
    logger->trace("\t Gap opening penalty: {}",
                  int64_t(config.alignment_gap_opening_penalty));
    logger->trace("\t Gap extension penalty: {}",
                  int64_t(config.alignment_gap_extension_penalty));
    logger->trace("\t Min DP table cell score: {}", int64_t(config.alignment_min_cell_score));
    logger->trace("\t Min alignment score: {}", config.alignment_min_path_score);

    logger->trace("\t Scoring matrix: {}", config.alignment_edit_distance ? "unit costs" : "matrix");
    if (!config.alignment_edit_distance) {
        logger->trace("\t\t Match score: {}", int64_t(config.alignment_match_score));
        logger->trace("\t\t (DNA) Transition score: {}",
                      int64_t(config.alignment_mm_transition_score));
        logger->trace("\t\t (DNA) Transversion score: {}",
                      int64_t(config.alignment_mm_transversion_score));
    }

    aligner_config.set_scoring_matrix();

    return aligner_config;
}

std::unique_ptr<IDBGAligner> build_aligner(const DeBruijnGraph &graph, Config &config) {
    DBGAlignerConfig aligner_config = initialize_aligner_config(graph, config);

    // TODO: fix this when alphabets are no longer set at compile time
    #if _PROTEIN_GRAPH
        const auto *alphabet = alphabets::kAlphabetProtein;
        const auto *alphabet_encoding = alphabets::kCharToProtein;
    #elif _DNA_CASE_SENSITIVE_GRAPH
        const auto *alphabet = alphabets::kAlphabetDNA;
        const auto *alphabet_encoding = alphabets::kCharToDNA;
    #elif _DNA5_GRAPH
        const auto *alphabet = alphabets::kAlphabetDNA;
        const auto *alphabet_encoding = alphabets::kCharToDNA;
    #elif _DNA_GRAPH
        const auto *alphabet = alphabets::kAlphabetDNA;
        const auto *alphabet_encoding = alphabets::kCharToDNA;
    #else
        static_assert(false,
            "Define an alphabet: either "
            "_DNA_GRAPH, _DNA5_GRAPH, _PROTEIN_GRAPH, or _DNA_CASE_SENSITIVE_GRAPH."
        );
    #endif

    Cigar::initialize_opt_table(alphabet, alphabet_encoding);

    if (config.alignment_seed_unimems) {
        return std::make_unique<DBGAligner<UniMEMSeeder<>>>(graph, aligner_config);

    } else if (config.alignment_min_seed_length < graph.get_k()) {
        if (!dynamic_cast<const DBGSuccinct*>(&graph)) {
            logger->error("SuffixSeeder can be used only with succinct graph representation");
            exit(1);
        }

        // Use the seeder that seeds to node suffixes
        return std::make_unique<DBGAligner<SuffixSeeder<>>>(graph, aligner_config);

    } else {
        return std::make_unique<DBGAligner<>>(graph, aligner_config);
    }
}

void map_sequences_in_file(const std::string &file,
                           const DeBruijnGraph &graph,
                           std::shared_ptr<DBGSuccinct> dbg,
                           const Config &config,
                           const Timer &timer,
                           ThreadPool *thread_pool = nullptr,
                           std::mutex *print_mutex = nullptr) {
    // TODO: multithreaded
    std::ignore = std::tie(thread_pool, print_mutex);

    Timer data_reading_timer;

    read_fasta_file_critical(file, [&](kseq_t *read_stream) {
        if (utils::get_verbose())
            std::cout << "Sequence: " << read_stream->seq.s << "\n";

        if (config.query_presence
                && config.alignment_length == graph.get_k()) {

            bool found = graph.find(read_stream->seq.s,
                                    config.discovery_fraction);

            if (!config.filter_present) {
                std::cout << found << "\n";

            } else if (found) {
                std::cout << ">" << read_stream->name.s << "\n"
                                 << read_stream->seq.s << "\n";
            }

            return;
        }

        assert(config.alignment_length <= graph.get_k());

        std::vector<DeBruijnGraph::node_index> graphindices;
        if (config.alignment_length == graph.get_k()) {
            graph.map_to_nodes(read_stream->seq.s,
                               [&](const auto &node) {
                                   graphindices.emplace_back(node);
                               });
        } else if (config.query_presence || config.count_kmers) {
            // TODO: make more efficient
            for (size_t i = 0; i + graph.get_k() <= read_stream->seq.l; ++i) {
                dbg->call_nodes_with_suffix(
                    std::string_view(read_stream->seq.s + i, config.alignment_length),
                    [&](auto node, auto) {
                        if (graphindices.empty())
                            graphindices.emplace_back(node);
                    },
                    config.alignment_length
                );
            }
        }

        size_t num_discovered = std::count_if(graphindices.begin(),
                                              graphindices.end(),
                                              [](const auto &x) { return x > 0; });

        const size_t num_kmers = graphindices.size();

        if (config.query_presence) {
            const size_t min_kmers_discovered =
                num_kmers - num_kmers * (1 - config.discovery_fraction);
            if (config.filter_present) {
                if (num_discovered >= min_kmers_discovered)
                    std::cout << ">" << read_stream->name.s << "\n"
                                     << read_stream->seq.s << "\n";
            } else {
                std::cout << (num_discovered >= min_kmers_discovered) << "\n";
            }
            return;
        }

        if (config.count_kmers) {
            std::cout << "Kmers matched (discovered/total): "
                      << num_discovered << "/"
                      << num_kmers << "\n";
            return;
        }

        if (config.alignment_length == graph.get_k()) {
            for (size_t i = 0; i < graphindices.size(); ++i) {
                assert(i + config.alignment_length <= read_stream->seq.l);
                std::cout << std::string_view(read_stream->seq.s + i, config.alignment_length)
                          << ": " << graphindices[i] << "\n";
            }
        } else {
            // map input subsequences to multiple nodes
            for (size_t i = 0; i + graph.get_k() <= read_stream->seq.l; ++i) {
                // TODO: make more efficient
                std::string_view subseq(read_stream->seq.s + i, config.alignment_length);

                dbg->call_nodes_with_suffix(subseq,
                                            [&](auto node, auto) {
                                                std::cout << subseq << ": "
                                                          << node
                                                          << "\n";
                                            },
                                            config.alignment_length);
            }
        }

    }, config.forward_and_reverse);

    logger->trace("File '{}' processed in {} sec, current mem usage: {} MiB, total time {} sec",
                  file, data_reading_timer.elapsed(), get_curr_RSS() >> 20, timer.elapsed());
}


int align_to_graph(Config *config) {
    assert(config);

    const auto &files = config->fnames;

    assert(config->infbase.size());

    // initialize aligner
    auto graph = load_critical_dbg(config->infbase);
    auto dbg = std::dynamic_pointer_cast<DBGSuccinct>(graph);

    // This speeds up mapping, and allows for node suffix matching
    if (dbg)
        dbg->reset_mask();

    Timer timer;
    ThreadPool thread_pool(std::max(1u, get_num_threads()) - 1);
    std::mutex print_mutex;

    if (config->map_sequences) {
        if (!config->alignment_length) {
            config->alignment_length = graph->get_k();
        } else if (config->alignment_length > graph->get_k()) {
            logger->warn("Mapping to k-mers longer than k is not supported");
            config->alignment_length = graph->get_k();
        }

        if (!dbg && config->alignment_length != graph->get_k()) {
            logger->error("Matching k-mers shorter than k only "
                          "supported for DBGSuccinct");
            exit(1);
        }

        logger->trace("Map sequences against the de Bruijn graph with k={}",
                      graph->get_k());
        logger->trace("Length of mapped k-mers: {}", config->alignment_length);

        for (const auto &file : files) {
            logger->trace("Map sequences from file '{}'", file);

            map_sequences_in_file(file,
                                  *graph,
                                  dbg,
                                  *config,
                                  timer,
                                  &thread_pool,
                                  &print_mutex);
        }

        thread_pool.join();

        return 0;
    }

    auto aligner = build_aligner(*graph, *config);

    for (const auto &file : files) {
        logger->info("Align sequences from file '{}'", file);

        Timer data_reading_timer;

        std::ostream *outstream = config->outfbase.size()
            ? new std::ofstream(config->outfbase)
            : &std::cout;

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";

        read_fasta_file_critical(file, [&](kseq_t *read_stream) {
            thread_pool.enqueue([&](std::string query,
                                    std::string header) {
                    auto paths = aligner->align(query);

                    std::ostringstream ostr;
                    if (!config->output_json) {
                        for (const auto &path : paths) {
                            const auto& path_query = path.get_orientation()
                                ? paths.get_query_reverse_complement()
                                : paths.get_query();

                            ostr << header << "\t"
                                 << path_query << "\t"
                                 << path
                                 << std::endl;
                        }

                        if (paths.empty())
                            ostr << header << "\t"
                                 << query << "\t"
                                 << "*\t*\t"
                                 << config->alignment_min_path_score << "\t*\t*\t*"
                                 << std::endl;
                    } else {
                        bool secondary = false;
                        for (const auto &path : paths) {
                            const auto& path_query = path.get_orientation()
                                ? paths.get_query_reverse_complement()
                                : paths.get_query();

                            ostr << Json::writeString(
                                        builder,
                                        path.to_json(path_query,
                                                     *graph,
                                                     secondary,
                                                     header)
                                    )
                                 << std::endl;

                            secondary = true;
                        }

                        if (paths.empty()) {
                            ostr << Json::writeString(
                                        builder,
                                        DBGAligner<>::DBGAlignment().to_json(
                                            query,
                                            *graph,
                                            secondary,
                                            header)
                                    )
                                 << std::endl;
                        }
                    }

                    auto lock = std::lock_guard<std::mutex>(print_mutex);
                    *outstream << ostr.str();
                },
                std::string(read_stream->seq.s),
                config->fasta_anno_comment_delim != Config::UNINITIALIZED_STR
                    && read_stream->comment.l
                        ? utils::join_strings(
                            { read_stream->name.s, read_stream->comment.s },
                            config->fasta_anno_comment_delim,
                            true)
                        : std::string(read_stream->name.s)
            );

            logger->trace("File '{}' processed in {} sec, "
                          "current mem usage: {} MiB, total time {} sec",
                          file, data_reading_timer.elapsed(),
                          get_curr_RSS() >> 20, timer.elapsed());
        });

        thread_pool.join();

        if (config->outfbase.size())
            delete outstream;
    }

    return 0;
}