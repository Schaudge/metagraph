#include "row_diff.hpp"

#include <filesystem>
#include <fstream>

#include "annotation/binary_matrix/column_sparse/column_major.hpp"
#include "annotation/binary_matrix/multi_brwt/brwt.hpp"
#include "annotation/binary_matrix/row_sparse/row_sparse.hpp"
#include "common/utils/file_utils.hpp"

namespace mtg {
namespace annot {

using node_index = graph::DeBruijnGraph::node_index;

node_index row_diff_successor(const graph::DeBruijnGraph &graph,
                              node_index node,
                              const bit_vector &rd_succ) {
    if (auto* dbg_succ = dynamic_cast<graph::DBGSuccinct const*>(&graph)) {
        return dbg_succ->get_boss().row_diff_successor(
            node,
            rd_succ.size() ? rd_succ : dbg_succ->get_boss().get_last()
        );
    } else {
        assert(rd_succ.size());
        node_index succ = graph::DeBruijnGraph::npos;
        graph.adjacent_outgoing_nodes(node, [&](node_index adjacent_node) {
            if (rd_succ[adjacent_node]) {
                succ = adjacent_node;
            }
        });
        assert(graph.in_graph(succ) && "a row diff successor must exist");
        return succ;
    }
}


namespace matrix {

void IRowDiff::load_anchor(const std::string &filename) {
    if (!std::filesystem::exists(filename)) {
        common::logger->error("Can't read anchor file: {}", filename);
        std::exit(1);
    }
    std::unique_ptr<std::ifstream> f = utils::open_ifstream(filename);
    if (!f->good()) {
        common::logger->error("Could not open anchor file {}", filename);
        std::exit(1);
    }
    anchor_.load(*f);
}

void IRowDiff::load_fork_succ(const std::string &filename) {
    if (!std::filesystem::exists(filename)) {
        common::logger->error("Can't read fork successor file: {}", filename);
        std::exit(1);
    }
    std::unique_ptr<std::ifstream> f = utils::open_ifstream(filename);
    if (!f->good()) {
        common::logger->error("Could not open fork successor file {}", filename);
        std::exit(1);
    }
    fork_succ_.load(*f);
}

std::tuple<std::vector<BinaryMatrix::Row>, std::vector<std::vector<size_t>>, std::vector<size_t>>
IRowDiff::get_rd_ids(const std::vector<BinaryMatrix::Row> &row_ids) const {
    assert(graph_ && "graph must be loaded");
    assert(!fork_succ_.size() || fork_succ_.size() == graph_->max_index() + 1);

    using Row = BinaryMatrix::Row;

    // map row index to its index in |rd_rows|
    VectorMap<Row, size_t> node_to_rd;
    node_to_rd.reserve(row_ids.size() * RD_PATH_RESERVE_SIZE);

    // Truncated row-diff paths, indexes to |rd_rows|.
    // The last index in each path points to an anchor or to a row which had
    // been reached before, and thus, will be reconstructed before this one.
    std::vector<std::vector<size_t>> rd_paths_trunc(row_ids.size());

    for (size_t i = 0; i < row_ids.size(); ++i) {
        Row row = row_ids[i];

        node_index node = graph::AnnotatedSequenceGraph::anno_to_graph_index(row);

        while (true) {
            assert(graph_->in_graph(node));
            row = graph::AnnotatedSequenceGraph::graph_to_anno_index(node);

            auto [it, is_new] = node_to_rd.try_emplace(row, node_to_rd.size());
            rd_paths_trunc[i].push_back(it.value());

            // If a node had been reached before, we interrupt the diff path.
            // The annotation for that node will have been reconstructed earlier
            // than for other nodes in this path as well. Thus, we will start
            // reconstruction from that node and don't need its successors.
            if (!is_new)
                break;

            if (anchor_[row])
                break;

            node = row_diff_successor(*graph_, node, fork_succ_);
        }
    }

    auto &m = const_cast<std::vector<std::pair<Row, size_t>>&>(node_to_rd.values_container());
    // sort by indexes of rd rows
    // the second value points to the index in batch
    std::sort(m.begin(), m.end(), utils::LessFirst());
    // collect an array of rd rows
    std::vector<Row> rd_ids(m.size());
    for (size_t i = 0; i < m.size(); ++i) {
        rd_ids[i] = m[i].first;
    }
    // make m[].first map indexes in batch to new indexes (where rows are sorted)
    for (size_t i = 0; i < m.size(); ++i) {
        m[m[i].second].first = i;
    }

    // keeps how many times rows in |rd_rows| will be queried
    std::vector<size_t> times_traversed(rd_ids.size(), 0);

    for (size_t i = 0; i < row_ids.size(); ++i) {
        for (auto &j : rd_paths_trunc[i]) {
            j = m[j].first;
            times_traversed[j]++;
        }
    }

    return std::make_tuple(std::move(rd_ids), std::move(rd_paths_trunc), std::move(times_traversed));
}

} // namespace matrix
} // namespace annot
} // namespace mtg
