//
// Created by Jan Studený on 2019-03-11.
//
#include <utility>
#include <iostream>
#include <map>
#include <filesystem>
#include <tclap/CmdLine.h>
#include <random>
#include <nlohmann/json.hpp>

using TCLAP::ValueArg;
using TCLAP::MultiArg;
using TCLAP::UnlabeledValueArg;
using TCLAP::UnlabeledMultiArg;
using TCLAP::ValuesConstraint;

#include "graph_patch.hpp"
#include "graph_statistics.hpp"
#include "utilities.hpp"



using namespace std;
using namespace nlohmann;
using namespace std::string_literals;

int main_statistics(int argc, char *argv[]) {
    TCLAP::CmdLine cmd("Compress reads",' ', "0.1");
    TCLAP::ValueArg<std::string> graphArg("g",
                                          "graph",
                                          "Graph to use as a reference in compression",
                                          true,
                                          "",
                                          "string",cmd);
    TCLAP::ValueArg<std::string> statisticsArg("s",
                                               "statistics",
                                               "Filename of json file that will output statistics about compressed file.",
                                               true,
                                               "statistics.json",
                                               "string",cmd);
    TCLAP::ValueArg<int> verbosityArg("v",
                                               "verbosity",
                                               "Level of detail of the statistics",
                                               false,
                                               0u,
                                               "int64_t",cmd);
    cmd.parse(argc, argv);
    auto graph = DBGSuccinct(21);
    graph.load(graphArg.getValue());
    auto statistics = get_statistics(graph,verbosityArg.getValue());
    cout << statistics << endl;
    save_string(statistics.dump(4),statisticsArg.getValue());
    return 0;
}

#undef int
