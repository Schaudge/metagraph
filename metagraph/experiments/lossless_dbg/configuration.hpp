//
// Created by Jan Studený on 2019-07-17.
//

#ifndef __CONFIGURATION_HPP__
#define __CONFIGURATION_HPP__

#include <sdsl/rrr_vector.hpp>
#include <sdsl/wt_rlmn.hpp>

//#define MASK_DUMMY_KMERS
using DefaultUnderlyingWaveletBV = sdsl::rrr_vector<>;
using DefaultWavelet = sdsl::wt_rlmn<DefaultUnderlyingWaveletBV,DefaultUnderlyingWaveletBV::rank_1_type,DefaultUnderlyingWaveletBV::select_1_type,sdsl::wt_huff<>>;
#endif
