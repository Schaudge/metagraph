#include "column_major.hpp"

#include "common/serialization.hpp"


ColumnMajor::ColumnMajor(std::vector<std::unique_ptr<bit_vector>>&& columns)
      : data_(std::move(columns)) {}

uint64_t ColumnMajor::num_rows() const {
    if (!columns_->size()) {
        return 0;
    } else {
        assert((*columns_)[0].get());
        return (*columns_)[0]->size();
    }
}

bool ColumnMajor::get(Row row, Column column) const {
    assert(column < columns_->size());
    assert((*columns_)[column].get());
    assert(row < (*columns_)[column]->size());
    return (*(*columns_)[column])[row];
}

ColumnMajor::SetBitPositions ColumnMajor::get_row(Row row) const {
    assert(row < num_rows());

    SetBitPositions result;
    for (size_t i = 0; i < columns_->size(); ++i) {
        assert((*columns_)[i].get());

        if ((*(*columns_)[i])[row])
            result.push_back(i);
    }
    return result;
}

std::vector<ColumnMajor::SetBitPositions>
ColumnMajor::get_rows(const std::vector<Row> &row_ids) const {
    std::vector<SetBitPositions> rows(row_ids.size());

    for (size_t j = 0; j < columns_->size(); ++j) {
        assert((*columns_)[j].get());
        const auto &col = (*(*columns_)[j]);

        for (size_t i = 0; i < row_ids.size(); ++i) {
            assert(row_ids[i] < num_rows());

            if (col[row_ids[i]])
                rows[i].push_back(j);
        }
    }

    return rows;
}

std::vector<ColumnMajor::Row> ColumnMajor::get_column(Column column) const {
    assert(column < columns_->size());
    assert((*columns_)[column].get());

    std::vector<Row> result;
    (*columns_)[column]->call_ones([&result](auto i) { result.push_back(i); });
    return result;
}

bool ColumnMajor::load(std::istream &in) {
    if (!in.good())
        return false;

    data_.clear();
    columns_ = &data_;

    try {
        data_.resize(load_number(in));

        for (auto &column : data_) {
            assert(!column.get());

            auto next = std::make_unique<bit_vector_sd>();
            if (!next->load(in))
                return false;
            column = std::move(next);
        }
        return true;
    } catch (...) {
        return false;
    }
}

void ColumnMajor::serialize(std::ostream &out) const {
    serialize_number(out, columns_->size());

    for (const auto &column : *columns_) {
        assert(column.get());
        column->serialize(out);
    }
}

// number of ones in the matrix
uint64_t ColumnMajor::num_relations() const {
    uint64_t num_set_bits = 0;

    for (const auto &column : *columns_) {
        assert(column.get());
        num_set_bits += column->num_set_bits();
    }
    return num_set_bits;
}