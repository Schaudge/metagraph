#include "load_annotation.hpp"

#include <string>

#include "common/logger.hpp"
#include "common/algorithms.hpp"
#include "annotation/representation/row_compressed/annotate_row_compressed.hpp"
#include "annotation/representation/column_compressed/annotate_column_compressed.hpp"
#include "annotation/representation/annotation_matrix/static_annotators_def.hpp"
#include "cli/config/config.hpp"


Config::AnnotationType parse_annotation_type(const std::string &filename) {
    if (utils::ends_with(filename, annotate::kColumnAnnotatorExtension)) {
        return Config::AnnotationType::ColumnCompressed;

    } else if (utils::ends_with(filename, annotate::kRowAnnotatorExtension)) {
        return Config::AnnotationType::RowCompressed;

    } else if (utils::ends_with(filename, annotate::kBRWTExtension)) {
        return Config::AnnotationType::BRWT;

    } else if (utils::ends_with(filename, annotate::kBinRelWT_sdslExtension)) {
        return Config::AnnotationType::BinRelWT_sdsl;

    } else if (utils::ends_with(filename, annotate::kBinRelWTExtension)) {
        return Config::AnnotationType::BinRelWT;

    } else if (utils::ends_with(filename, annotate::kRowPackedExtension)) {
        return Config::AnnotationType::RowFlat;

    } else if (utils::ends_with(filename, annotate::kRainbowfishExtension)) {
        return Config::AnnotationType::RBFish;

    } else {
        mg::common::logger->error("Unknown annotation format in '{}'", filename);
        exit(1);
    }
}

std::unique_ptr<annotate::MultiLabelEncoded<std::string>>
initialize_annotation(Config::AnnotationType anno_type,
                      const Config &config,
                      uint64_t num_rows) {
    std::unique_ptr<annotate::MultiLabelEncoded<std::string>> annotation;

    switch (anno_type) {
        case Config::ColumnCompressed: {
            annotation.reset(
                new annotate::ColumnCompressed<>(
                    num_rows, config.num_columns_cached, utils::get_verbose()
                )
            );
            break;
        }
        case Config::RowCompressed: {
            annotation.reset(new annotate::RowCompressed<>(num_rows, config.sparse));
            break;
        }
        case Config::BRWT: {
            annotation.reset(new annotate::BRWTCompressed<>(config.row_cache_size));
            break;
        }
        case Config::BinRelWT_sdsl: {
            annotation.reset(new annotate::BinRelWT_sdslAnnotator(config.row_cache_size));
            break;
        }
        case Config::BinRelWT: {
            annotation.reset(new annotate::BinRelWTAnnotator(config.row_cache_size));
            break;
        }
        case Config::RowFlat: {
            annotation.reset(new annotate::RowFlatAnnotator(config.row_cache_size));
            break;
        }
        case Config::RBFish: {
            annotation.reset(new annotate::RainbowfishAnnotator(config.row_cache_size));
            break;
        }
    }

    return annotation;
}