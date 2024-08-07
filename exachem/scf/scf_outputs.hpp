/*
 * ExaChem: Open Source Exascale Computational Chemistry Software.
 *
 * Copyright 2023-2024 Pacific Northwest National Laboratory, Battelle Memorial Institute.
 *
 * See LICENSE.txt for details
 */

#pragma once

#include "exachem/common/cutils.hpp"
#include "exachem/common/ec_basis.hpp"

#include "exachem/scf/scf_compute.hpp"
#include "exachem/scf/scf_eigen_tensors.hpp"
#include "exachem/scf/scf_matrix.hpp"
#include "exachem/scf/scf_tamm_tensors.hpp"
#include "exachem/scf/scf_vars.hpp"

namespace exachem::scf {

class SCFIO: public SCFMatrix {
private:
  template<typename TensorType>
  double tt_trace(ExecutionContext& ec, Tensor<TensorType>& T1, Tensor<TensorType>& T2);

public:
  void rw_md_disk(ExecutionContext& ec, const ChemEnv& chem_env, ScalapackInfo& scalapack_info,
                  TAMMTensors& ttensors, EigenTensors& etensors, std::string files_prefix,
                  bool read = false);

  template<typename T>
  void rw_mat_disk(Tensor<T> tensor, std::string tfilename, bool profile, bool read = false);
  void print_mulliken(ChemEnv& chem_env, Matrix& D, Matrix& D_beta, Matrix& S);
  void print_energies(ExecutionContext& ec, ChemEnv& chem_env, TAMMTensors& ttensors,
                      EigenTensors& etensors, SCFVars& scf_vars, ScalapackInfo& scalapack_info);
};
} // namespace exachem::scf
