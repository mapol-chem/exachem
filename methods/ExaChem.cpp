/*
 * ExaChem: Open Source Exascale Computational Chemistry Software.
 *
 * Copyright 2023 Pacific Northwest National Laboratory, Battelle Memorial Institute.
 *
 * See LICENSE.txt for details
 */

// clang-format off
#include <tamm/tamm_git.hpp>
#include <exachem/exachem_git.hpp>
#include "cc/ccsd/cd_ccsd_os_ann.hpp"
#include "cc/ccsd_t/ccsd_t_fused_driver.hpp"
#include "exachem/cc/lambda/ccsd_lambda.hpp"
#include "exachem/cc/eom/eomccsd_opt.hpp"

#include "exachem/common/chemenv.hpp"
#include "exachem/common/options/parse_options.hpp"
#include "scf/scf_main.hpp"
// clang-format on

#define EC_COMPLEX

#if !defined(USE_UPCXX) and defined(EC_COMPLEX)
void gfccsd_driver(ExecutionContext& ec, ChemEnv& chem_env);
void rt_eom_cd_ccsd_driver(ExecutionContext& ec, ChemEnv& chem_env);
#include "exachem/fci/fci.hpp"
#endif

void cd_cc2_driver(ExecutionContext& ec, ChemEnv& chem_env);
void ducc_driver(ExecutionContext& ec, ChemEnv& chem_env);

int main(int argc, char* argv[]) {
  tamm::initialize(argc, argv);

  if(argc < 2) tamm_terminate("Please provide an input file or folder!");

  auto                     input_fpath = std::string(argv[1]);
  std::vector<std::string> inputfiles;

  if(fs::is_directory(input_fpath)) {
    for(auto const& dir_entry: std::filesystem::directory_iterator{input_fpath}) {
      if(fs::path(dir_entry.path()).extension() == ".json") inputfiles.push_back(dir_entry.path());
    }
  }
  else {
    if(!fs::exists(input_fpath))
      tamm_terminate("Input file or folder path provided [" + input_fpath + "] does not exist!");
    inputfiles.push_back(input_fpath);
  }

  if(inputfiles.empty()) tamm_terminate("No input files provided");

  const auto       rank = ProcGroup::world_rank();
  ProcGroup        pg   = ProcGroup::create_world_coll();
  ExecutionContext ec{pg, DistributionKind::nw, MemoryManagerKind::ga};

  for(auto ifile: inputfiles) {
    std::string   input_file = fs::canonical(ifile);
    std::ifstream testinput(input_file);
    if(!testinput) tamm_terminate("Input file provided [" + input_file + "] does not exist!");

    auto current_time   = std::chrono::system_clock::now();
    auto current_time_t = std::chrono::system_clock::to_time_t(current_time);
    auto cur_local_time = localtime(&current_time_t);

    // read geometry from a json file
    ChemEnv chem_env;
    chem_env.input_file = input_file;

    // This call should update all input options and SystemData object
    std::unique_ptr<ECOptionParser> iparse = std::make_unique<ECOptionParser>(chem_env);

    ECOptions& ioptions              = chem_env.ioptions;
    chem_env.sys_data.input_molecule = ParserUtils::getfilename(input_file);

    if(chem_env.ioptions.common_options.file_prefix.empty()) {
      chem_env.ioptions.common_options.file_prefix = chem_env.sys_data.input_molecule;
    }

    chem_env.sys_data.output_file_prefix =
      chem_env.ioptions.common_options.file_prefix + "." + chem_env.ioptions.common_options.basis;
    chem_env.workspace_dir = chem_env.sys_data.output_file_prefix + "_files/";

    if(rank == 0) {
      std::cout << exachem_git_info() << std::endl;
      std::cout << tamm_git_info() << std::endl;
    }

    if(rank == 0) {
      cout << endl << "date: " << std::put_time(cur_local_time, "%c") << endl;
      cout << "program: " << fs::canonical(argv[0]) << endl;
      cout << "input: " << input_file << endl;
      cout << "nnodes: " << ec.nnodes() << ", ";
      cout << "nproc: " << ec.nnodes() * ec.ppn() << endl;
      cout << "prefix: " << chem_env.sys_data.output_file_prefix << endl << endl;
      ec.print_mem_info();
      cout << endl << endl;
      cout << "Input file provided" << endl << std::string(20, '-') << endl;
      std::cout << chem_env.jinput.dump(2) << std::endl;
    }

    const auto              task = ioptions.task_options;
    const std::vector<bool> tvec = {task.sinfo,
                                    task.scf,
                                    task.mp2,
                                    task.gw,
                                    task.fci,
                                    task.cd_2e,
                                    task.ducc,
                                    task.ccsd,
                                    task.ccsd_t,
                                    task.ccsd_lambda,
                                    task.eom_ccsd,
                                    task.fcidump,
                                    task.rteom_cc2,
                                    task.rteom_ccsd,
                                    task.gfccsd,
                                    task.dlpno_ccsd.first,
                                    task.dlpno_ccsd_t.first};
    if(std::count(tvec.begin(), tvec.end(), true) > 1)
      tamm_terminate("[INPUT FILE ERROR] only a single task can be enabled at once!");

    std::string ec_arg2{};
    if(argc == 3) {
      ec_arg2 = std::string(argv[2]);
      if(!fs::exists(ec_arg2))
        tamm_terminate("Input file provided [" + ec_arg2 + "] does not exist!");
    }

#if !defined(USE_MACIS)
    if(task.fci) tamm_terminate("Full CI integration not enabled!");
#endif

    if(task.sinfo) chem_env.sinfo();
    else if(task.scf) scf(ec, chem_env);
    else if(task.mp2) cd_mp2(ec, chem_env);
    else if(task.cd_2e) cd_2e_driver(ec, chem_env);
    else if(task.ccsd) cd_ccsd(ec, chem_env);
    else if(task.ccsd_t) ccsd_t_driver(ec, chem_env);
    else if(task.cc2) cd_cc2_driver(ec, chem_env);
    else if(task.ccsd_lambda) ccsd_lambda_driver(ec, chem_env);
    else if(task.eom_ccsd) eom_ccsd_driver(ec, chem_env);
    else if(task.ducc) ducc_driver(ec, chem_env);
#if !defined(USE_UPCXX) and defined(EC_COMPLEX)
    else if(task.fci || task.fcidump) fci_driver(ec, chem_env);
    else if(task.gfccsd) gfccsd_driver(ec, chem_env);
    else if(task.rteom_ccsd) rt_eom_cd_ccsd_driver(ec, chem_env);
#endif

    else
      tamm_terminate(
        "[ERROR] Unsupported task specified (or) code for the specified task is not built");
  }

  tamm::finalize();

  return 0;
}
