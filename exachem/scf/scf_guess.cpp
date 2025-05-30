/*
 * ExaChem: Open Source Exascale Computational Chemistry Software.
 *
 * Copyright 2023-2024 Pacific Northwest National Laboratory, Battelle Memorial Institute.
 *
 * See LICENSE.txt for details
 */

#include "exachem/scf/scf_guess.hpp"
#include <algorithm>
#include <iterator>

/// computes orbital occupation numbers for a subshell of size \c size created
/// by smearing
/// no more than \c ne electrons (corresponds to spherical averaging)
///
/// @param[in,out] occvec occupation vector, increments by \c size on return
/// @param[in] size the size of the subshell
/// @param[in,out] ne the number of electrons, on return contains the number of
/// "remaining" electrons
template<typename Real>
void exachem::scf::scf_guess::subshell_occvec(Real& occvec, size_t size, size_t& ne) {
  const auto ne_alloc = (ne > 2 * size) ? 2 * size : ne;
  ne -= ne_alloc;
  occvec += ne_alloc;
}

/// @brief computes the number of electrons is s, p, d, and f shells
/// @return occupation vector corresponding to the ground state electronic
///         configuration of a neutral atom with atomic number \c Z
template<typename Real>
const std::vector<Real> exachem::scf::scf_guess::compute_ao_occupation_vector(size_t Z) {
  std::vector<Real> occvec(4, 0.0);
  size_t            num_of_electrons = Z; // # of electrons to allocate

  // neutral atom electronic configurations from NIST:
  // http://www.nist.gov/pml/data/images/illo_for_2014_PT_1.PNG
  subshell_occvec(occvec[0], 1, num_of_electrons);   // 1s
  if(Z > 2) {                                        // Li+
    subshell_occvec(occvec[0], 1, num_of_electrons); // 2s
    subshell_occvec(occvec[1], 3, num_of_electrons); // 2p
  }
  if(Z > 10) {                                       // Na+
    subshell_occvec(occvec[0], 1, num_of_electrons); // 3s
    subshell_occvec(occvec[1], 3, num_of_electrons); // 3p
  }
  if(Z > 18) { // K .. Kr
    // NB 4s is singly occupied in K, Cr, and Cu
    size_t num_of_4s_electrons = (Z == 19 || Z == 24 || Z == 29) ? 1 : 2;
    num_of_electrons -= num_of_4s_electrons;
    subshell_occvec(occvec[0], 1, num_of_4s_electrons); // 4s
    subshell_occvec(occvec[2], 5, num_of_electrons);    // 3d
    subshell_occvec(occvec[1], 3, num_of_electrons);    // 4p
  }
  if(Z > 36) { // Rb .. I

    // NB 5s is singly occupied in Rb, Nb, Mo, Ru, Rh, and Ag
    size_t num_of_5s_electrons = (Z == 37 || Z == 41 || Z == 42 || Z == 44 || Z == 45 || Z == 47)
                                   ? 1
                                 : (Z == 46) ? 0
                                             : 2;
    num_of_electrons -= num_of_5s_electrons;
    subshell_occvec(occvec[0], 1, num_of_5s_electrons); // 5s
    subshell_occvec(occvec[2], 5, num_of_electrons);    // 4d
    subshell_occvec(occvec[1], 3, num_of_electrons);    // 5p
  }
  if(Z > 54) { // Cs .. Rn
    size_t num_of_6s_electrons = (Z == 55 || Z == 78 || Z == 79) ? 1 : 2;
    num_of_electrons -= num_of_6s_electrons;
    subshell_occvec(occvec[0], 1, num_of_6s_electrons); // 6s
    size_t num_of_5d_electrons = (Z == 57 || Z == 58 || Z == 64) ? 1 : 0;
    num_of_electrons -= num_of_5d_electrons;
    subshell_occvec(occvec[2], 5, num_of_5d_electrons); // 5d (Lanthanides)
    subshell_occvec(occvec[3], 7, num_of_electrons);    // 4f
    subshell_occvec(occvec[2], 5, num_of_electrons);    // 5d
    subshell_occvec(occvec[1], 3, num_of_electrons);    // 6p
  }
  if(Z > 86) {                                       // Fr .. Og
    subshell_occvec(occvec[0], 1, num_of_electrons); // 7s
    size_t num_of_6d_electrons = (Z == 89 || Z == 91 || Z == 92 || Z == 93 || Z == 96) ? 1
                                 : (Z == 90)                                           ? 2
                                                                                       : 0;
    num_of_electrons -= num_of_6d_electrons;
    subshell_occvec(occvec[2], 5, num_of_6d_electrons); // 6d (Actinides)
    subshell_occvec(occvec[3], 7, num_of_electrons);    // 5f
    size_t num_of_7p_electrons = (Z == 103) ? 1 : 0;
    num_of_electrons -= num_of_7p_electrons;
    subshell_occvec(occvec[1], 3, num_of_7p_electrons); // 7p (Lawrencium)
    subshell_occvec(occvec[2], 5, num_of_electrons);    // 6d
    subshell_occvec(occvec[1], 3, num_of_electrons);    // 7p
  }
  return occvec;
}

// computes Superposition-Of-Atomic-Densities guess for the molecular density matrix
// in minimal basis; occupies subshells by smearing electrons evenly over the orbitals
Matrix exachem::scf::SCFGuess::compute_soad(const std::vector<Atom>& atoms) {
  // compute number of atomic orbitals
  size_t natoms = atoms.size();
  size_t offset = 0;

  // compute the minimal basis density
  Matrix D = Matrix::Zero(natoms, 4);
  for(const auto& atom: atoms) {
    const auto Z      = atom.atomic_number;
    const auto occvec = scf_guess::compute_ao_occupation_vector(Z);
    for(int i = 0; i < 4; ++i) D(offset, i) = occvec[i];
    ++offset;
  }
  return D; // we use densities normalized to # of electrons/2
}

template<typename TensorType>
void exachem::scf::SCFGuess::compute_dipole_ints(
  ExecutionContext& ec, const SCFVars& spvars, Tensor<TensorType>& tensorX,
  Tensor<TensorType>& tensorY, Tensor<TensorType>& tensorZ, std::vector<libint2::Atom>& atoms,
  libint2::BasisSet& shells, libint2::Operator otype) {
  using libint2::Atom;
  using libint2::BasisSet;
  using libint2::Engine;
  using libint2::Operator;
  using libint2::Shell;

  const std::vector<Tile>&   AO_tiles       = spvars.AO_tiles;
  const std::vector<size_t>& shell_tile_map = spvars.shell_tile_map;

  Engine engine(otype, max_nprim(shells), max_l(shells), 0);

  // engine.set(otype);
  // auto& buf = (engine.results());

  auto compute_dipole_ints_lambda = [&](const IndexVector& blockid) {
    auto bi0 = blockid[0];
    auto bi1 = blockid[1];

    const TAMM_SIZE         size       = tensorX.block_size(blockid);
    auto                    block_dims = tensorX.block_dims(blockid);
    std::vector<TensorType> dbufX(size);
    std::vector<TensorType> dbufY(size);
    std::vector<TensorType> dbufZ(size);

    auto bd1 = block_dims[1];

    // auto s1 = blockid[0];
    auto                  s1range_end   = shell_tile_map[bi0];
    decltype(s1range_end) s1range_start = 0l;
    if(bi0 > 0) s1range_start = shell_tile_map[bi0 - 1] + 1;

    // cout << "s1-start,end = " << s1range_start << ", " << s1range_end << endl;
    for(auto s1 = s1range_start; s1 <= s1range_end; ++s1) {
      // auto bf1 = shell2bf[s1]; //shell2bf[s1]; // first basis function in
      // this shell
      auto n1 = shells[s1].size();

      auto                  s2range_end   = shell_tile_map[bi1];
      decltype(s2range_end) s2range_start = 0l;
      if(bi1 > 0) s2range_start = shell_tile_map[bi1 - 1] + 1;

      // cout << "s2-start,end = " << s2range_start << ", " << s2range_end << endl;

      // cout << "screend shell pair list = " << s2spl << endl;
      for(auto s2 = s2range_start; s2 <= s2range_end; ++s2) {
        // for (auto s2: spvars.obs_shellpair_list.at(s1)) {
        // auto s2 = blockid[1];
        // if (s2>s1) continue;

        if(s2 > s1) {
          auto s2spl = spvars.obs_shellpair_list.at(s2);
          if(std::find(s2spl.begin(), s2spl.end(), s1) == s2spl.end()) continue;
        }
        else {
          auto s2spl = spvars.obs_shellpair_list.at(s1);
          if(std::find(s2spl.begin(), s2spl.end(), s2) == s2spl.end()) continue;
        }

        // auto bf2 = shell2bf[s2];
        auto n2 = shells[s2].size();

        std::vector<TensorType> tbufX(n1 * n2);
        std::vector<TensorType> tbufY(n1 * n2);
        std::vector<TensorType> tbufZ(n1 * n2);

        // compute shell pair; return is the pointer to the buffer
        const auto& buf = engine.compute(shells[s1], shells[s2]);
        EXPECTS(buf.size() >= 4);
        if(buf[0] == nullptr) continue;
        // "map" buffer to a const Eigen Matrix, and copy it to the
        // corresponding blocks of the result

        Eigen::Map<const Matrix> buf_mat_X(buf[1], n1, n2);
        Eigen::Map<Matrix>(&tbufX[0], n1, n2) = buf_mat_X;
        Eigen::Map<const Matrix> buf_mat_Y(buf[2], n1, n2);
        Eigen::Map<Matrix>(&tbufY[0], n1, n2) = buf_mat_Y;
        Eigen::Map<const Matrix> buf_mat_Z(buf[3], n1, n2);
        Eigen::Map<Matrix>(&tbufZ[0], n1, n2) = buf_mat_Z;

        auto curshelloffset_i = 0U;
        auto curshelloffset_j = 0U;
        for(auto x = s1range_start; x < s1; x++) curshelloffset_i += AO_tiles[x];
        for(auto x = s2range_start; x < s2; x++) curshelloffset_j += AO_tiles[x];

        size_t c    = 0;
        auto   dimi = curshelloffset_i + AO_tiles[s1];
        auto   dimj = curshelloffset_j + AO_tiles[s2];

        for(size_t i = curshelloffset_i; i < dimi; i++) {
          for(size_t j = curshelloffset_j; j < dimj; j++, c++) {
            dbufX[i * bd1 + j] = tbufX[c];
            dbufY[i * bd1 + j] = tbufY[c];
            dbufZ[i * bd1 + j] = tbufZ[c];
          }
        }
      } // s2
    }   // s1

    tensorX.put(blockid, dbufX);
    tensorY.put(blockid, dbufY);
    tensorZ.put(blockid, dbufZ);
  };

  block_for(ec, tensorX(), compute_dipole_ints_lambda);
}

template<typename TensorType>
void exachem::scf::SCFGuess::compute_1body_ints(ExecutionContext& ec, const SCFVars& scf_vars,
                                                Tensor<TensorType>&         tensor1e,
                                                std::vector<libint2::Atom>& atoms,
                                                libint2::BasisSet&          shells,
                                                libint2::Operator           otype) {
  using libint2::Atom;
  using libint2::BasisSet;
  using libint2::Engine;
  using libint2::Operator;
  using libint2::Shell;

  const std::vector<Tile>&   AO_tiles       = scf_vars.AO_tiles;
  const std::vector<size_t>& shell_tile_map = scf_vars.shell_tile_map;

  Engine engine(otype, max_nprim(shells), max_l(shells), 0);

  // engine.set(otype);

  if(otype == Operator::nuclear) {
    std::vector<std::pair<double, std::array<double, 3>>> q;
    for(const auto& atom: atoms)
      q.push_back({static_cast<double>(atom.atomic_number), {{atom.x, atom.y, atom.z}}});

    engine.set_params(q);
  }

  auto& buf = (engine.results());

  auto compute_1body_ints_lambda = [&](const IndexVector& blockid) {
    auto bi0 = blockid[0];
    auto bi1 = blockid[1];

    const TAMM_SIZE         size       = tensor1e.block_size(blockid);
    auto                    block_dims = tensor1e.block_dims(blockid);
    std::vector<TensorType> dbuf(size);

    auto bd1 = block_dims[1];

    // auto s1 = blockid[0];
    auto                  s1range_end   = shell_tile_map[bi0];
    decltype(s1range_end) s1range_start = 0l;
    if(bi0 > 0) s1range_start = shell_tile_map[bi0 - 1] + 1;

    // cout << "s1-start,end = " << s1range_start << ", " << s1range_end << endl;
    for(auto s1 = s1range_start; s1 <= s1range_end; ++s1) {
      // auto bf1 = shell2bf[s1]; //shell2bf[s1]; // first basis function in
      // this shell
      auto n1 = shells[s1].size();

      auto                  s2range_end   = shell_tile_map[bi1];
      decltype(s2range_end) s2range_start = 0l;
      if(bi1 > 0) s2range_start = shell_tile_map[bi1 - 1] + 1;

      // cout << "s2-start,end = " << s2range_start << ", " << s2range_end << endl;

      // cout << "screend shell pair list = " << s2spl << endl;
      for(auto s2 = s2range_start; s2 <= s2range_end; ++s2) {
        // for (auto s2: scf_vars.obs_shellpair_list.at(s1)) {
        // auto s2 = blockid[1];
        // if (s2>s1) continue;

        if(s2 > s1) {
          auto s2spl = scf_vars.obs_shellpair_list.at(s2);
          if(std::find(s2spl.begin(), s2spl.end(), s1) == s2spl.end()) continue;
        }
        else {
          auto s2spl = scf_vars.obs_shellpair_list.at(s1);
          if(std::find(s2spl.begin(), s2spl.end(), s2) == s2spl.end()) continue;
        }

        // auto bf2 = shell2bf[s2];
        auto n2 = shells[s2].size();

        std::vector<TensorType> tbuf(n1 * n2);

        // compute shell pair; return is the pointer to the buffer
        engine.compute(shells[s1], shells[s2]);
        if(buf[0] == nullptr) continue;
        // "map" buffer to a const Eigen Matrix, and copy it to the
        // corresponding blocks of the result
        Eigen::Map<const Matrix> buf_mat(buf[0], n1, n2);
        Eigen::Map<Matrix>(&tbuf[0], n1, n2) = buf_mat;
        // tensor1e.put(blockid, tbuf);

        auto curshelloffset_i = 0U;
        auto curshelloffset_j = 0U;
        for(auto x = s1range_start; x < s1; x++) curshelloffset_i += AO_tiles[x];
        for(auto x = s2range_start; x < s2; x++) curshelloffset_j += AO_tiles[x];

        size_t c    = 0;
        auto   dimi = curshelloffset_i + AO_tiles[s1];
        auto   dimj = curshelloffset_j + AO_tiles[s2];

        for(size_t i = curshelloffset_i; i < dimi; i++) {
          for(size_t j = curshelloffset_j; j < dimj; j++, c++) { dbuf[i * bd1 + j] = tbuf[c]; }
        }

        // if(s1!=s2){
        //     std::vector<TensorType> ttbuf(n1*n2);
        //     Eigen::Map<Matrix>(ttbuf.data(),n2,n1) = buf_mat.transpose();
        //     // Matrix buf_mat_trans = buf_mat.transpose();
        //     size_t c = 0;
        //     for(size_t j = curshelloffset_j; j < dimj; j++) {
        //       for(size_t i = curshelloffset_i; i < dimi; i++, c++) {
        //             dbuf[j*block_dims[0]+i] = ttbuf[c];
        //       }
        //     }
        // }
        // tensor1e.put({s2,s1}, ttbuf);
      }
    }
    tensor1e.put(blockid, dbuf);
  };

  block_for(ec, tensor1e(), compute_1body_ints_lambda);
}

template<typename TensorType>
void exachem::scf::SCFGuess::compute_ecp_ints(ExecutionContext& ec, const SCFVars& scf_vars,
                                              Tensor<TensorType>&                    tensor1e,
                                              std::vector<libecpint::GaussianShell>& shells,
                                              std::vector<libecpint::ECP>&           ecps) {
  const std::vector<Tile>&   AO_tiles       = scf_vars.AO_tiles;
  const std::vector<size_t>& shell_tile_map = scf_vars.shell_tile_map;

  int maxam     = 0;
  int ecp_maxam = 0;
  for(auto shell: shells)
    if(shell.l > maxam) maxam = shell.l;
  for(auto ecp: ecps)
    if(ecp.L > ecp_maxam) ecp_maxam = ecp.L;

  size_t  size_       = (maxam + 1) * (maxam + 2) * (maxam + 1) * (maxam + 2) / 4;
  double* buffer_     = new double[size_];
  double* buffer_sph_ = new double[size_];
  memset(buffer_, 0, size_ * sizeof(double));

  libecpint::ECPIntegral engine(maxam, ecp_maxam, 0, 1e-17, 1024, 2048);

  auto compute_ecp_ints_lambda = [&](const IndexVector& blockid) {
    auto bi0 = blockid[0];
    auto bi1 = blockid[1];

    const TAMM_SIZE         size       = tensor1e.block_size(blockid);
    auto                    block_dims = tensor1e.block_dims(blockid);
    std::vector<TensorType> dbuf(size);

    auto bd1 = block_dims[1];

    // auto s1 = blockid[0];
    auto                  s1range_end   = shell_tile_map[bi0];
    decltype(s1range_end) s1range_start = 0l;
    if(bi0 > 0) s1range_start = shell_tile_map[bi0 - 1] + 1;

    // cout << "s1-start,end = " << s1range_start << ", " << s1range_end << endl;
    for(auto s1 = s1range_start; s1 <= s1range_end; ++s1) {
      // auto bf1 = shell2bf[s1]; //shell2bf[s1]; // first basis function in
      // this shell
      auto n1 = 2 * shells[s1].l + 1;

      auto                  s2range_end   = shell_tile_map[bi1];
      decltype(s2range_end) s2range_start = 0l;
      if(bi1 > 0) s2range_start = shell_tile_map[bi1 - 1] + 1;

      // cout << "s2-start,end = " << s2range_start << ", " << s2range_end << endl;

      // cout << "screend shell pair list = " << s2spl << endl;
      for(auto s2 = s2range_start; s2 <= s2range_end; ++s2) {
        // for (auto s2: scf_vars.obs_shellpair_list.at(s1)) {
        // auto s2 = blockid[1];
        // if (s2>s1) continue;

        if(s2 > s1) {
          auto s2spl = scf_vars.obs_shellpair_list.at(s2);
          if(std::find(s2spl.begin(), s2spl.end(), s1) == s2spl.end()) continue;
        }
        else {
          auto s2spl = scf_vars.obs_shellpair_list.at(s1);
          if(std::find(s2spl.begin(), s2spl.end(), s2) == s2spl.end()) continue;
        }

        // auto bf2 = shell2bf[s2];
        auto n2 = 2 * shells[s2].l + 1;

        std::vector<TensorType> tbuf(n1 * n2);
        // cout << "s1,s2,n1,n2 = "  << s1 << "," << s2 <<
        //       "," << n1 <<"," << n2 <<endl;

        // compute shell pair; return is the pointer to the buffer
        const libecpint::GaussianShell& LibECPShell1 = shells[s1];
        const libecpint::GaussianShell& LibECPShell2 = shells[s2];
        size_ = shells[s1].ncartesian() * shells[s2].ncartesian();
        memset(buffer_, 0, size_ * sizeof(double));
        for(const auto& ecp: ecps) {
          libecpint::TwoIndex<double> results;
          engine.compute_shell_pair(ecp, LibECPShell1, LibECPShell2, results);
          // for (auto v: results.data) std::cout << std::setprecision(6) << v << std::endl;
          std::transform(results.data.begin(), results.data.end(), buffer_, buffer_,
                         std::plus<double>());
        }
        libint2::solidharmonics::tform(shells[s1].l, shells[s2].l, buffer_, buffer_sph_);

        // "map" buffer to a const Eigen Matrix, and copy it to the
        // corresponding blocks of the result
        Eigen::Map<const Matrix> buf_mat(&buffer_sph_[0], n1, n2);
        Eigen::Map<Matrix>(&tbuf[0], n1, n2) = buf_mat;
        // tensor1e.put(blockid, tbuf);

        auto curshelloffset_i = 0U;
        auto curshelloffset_j = 0U;
        for(auto x = s1range_start; x < s1; x++) curshelloffset_i += AO_tiles[x];
        for(auto x = s2range_start; x < s2; x++) curshelloffset_j += AO_tiles[x];

        size_t c    = 0;
        auto   dimi = curshelloffset_i + AO_tiles[s1];
        auto   dimj = curshelloffset_j + AO_tiles[s2];

        // cout << "curshelloffset_i,curshelloffset_j,dimi,dimj = "  << curshelloffset_i << "," <<
        // curshelloffset_j <<
        //       "," << dimi <<"," << dimj <<endl;

        for(size_t i = curshelloffset_i; i < dimi; i++) {
          for(size_t j = curshelloffset_j; j < dimj; j++, c++) { dbuf[i * bd1 + j] = tbuf[c]; }
        }

        // if(s1!=s2){
        //     std::vector<TensorType> ttbuf(n1*n2);
        //     Eigen::Map<Matrix>(ttbuf.data(),n2,n1) = buf_mat.transpose();
        //     // Matrix buf_mat_trans = buf_mat.transpose();
        //     size_t c = 0;
        //     for(size_t j = curshelloffset_j; j < dimj; j++) {
        //       for(size_t i = curshelloffset_i; i < dimi; i++, c++) {
        //             dbuf[j*block_dims[0]+i] = ttbuf[c];
        //       }
        //     }
        // }
        // tensor1e.put({s2,s1}, ttbuf);
      }
    }
    tensor1e.put(blockid, dbuf);
  };
  block_for(ec, tensor1e(), compute_ecp_ints_lambda);
  delete[] buffer_;
  delete[] buffer_sph_;
} // END of compute_ecp_ints

template<typename TensorType>
void exachem::scf::SCFGuess::compute_pchg_ints(
  ExecutionContext& ec, const SCFVars& scf_vars, Tensor<TensorType>& tensor1e,
  std::vector<std::pair<double, std::array<double, 3>>>& q, libint2::BasisSet& shells,
  libint2::Operator otype) {
  using libint2::Atom;
  using libint2::BasisSet;
  using libint2::Engine;
  using libint2::Operator;
  using libint2::Shell;

  const std::vector<Tile>&   AO_tiles       = scf_vars.AO_tiles;
  const std::vector<size_t>& shell_tile_map = scf_vars.shell_tile_map;

  Engine engine(otype, max_nprim(shells), max_l(shells), 0);

  // engine.set(otype);
  engine.set_params(q);

  auto& buf = (engine.results());

  auto compute_pchg_ints_lambda = [&](const IndexVector& blockid) {
    auto bi0 = blockid[0];
    auto bi1 = blockid[1];

    const TAMM_SIZE         size       = tensor1e.block_size(blockid);
    auto                    block_dims = tensor1e.block_dims(blockid);
    std::vector<TensorType> dbuf(size);

    auto bd1 = block_dims[1];

    // auto s1 = blockid[0];
    auto                  s1range_end   = shell_tile_map[bi0];
    decltype(s1range_end) s1range_start = 0l;
    if(bi0 > 0) s1range_start = shell_tile_map[bi0 - 1] + 1;

    // cout << "s1-start,end = " << s1range_start << ", " << s1range_end << endl;
    for(auto s1 = s1range_start; s1 <= s1range_end; ++s1) {
      // auto bf1 = shell2bf[s1]; //shell2bf[s1]; // first basis function in
      // this shell
      auto n1 = shells[s1].size();

      auto                  s2range_end   = shell_tile_map[bi1];
      decltype(s2range_end) s2range_start = 0l;
      if(bi1 > 0) s2range_start = shell_tile_map[bi1 - 1] + 1;

      // cout << "s2-start,end = " << s2range_start << ", " << s2range_end << endl;

      // cout << "screend shell pair list = " << s2spl << endl;
      for(auto s2 = s2range_start; s2 <= s2range_end; ++s2) {
        // for (auto s2: scf_vars.obs_shellpair_list.at(s1)) {
        // auto s2 = blockid[1];
        // if (s2>s1) continue;

        if(s2 > s1) {
          auto s2spl = scf_vars.obs_shellpair_list.at(s2);
          if(std::find(s2spl.begin(), s2spl.end(), s1) == s2spl.end()) continue;
        }
        else {
          auto s2spl = scf_vars.obs_shellpair_list.at(s1);
          if(std::find(s2spl.begin(), s2spl.end(), s2) == s2spl.end()) continue;
        }

        // auto bf2 = shell2bf[s2];
        auto n2 = shells[s2].size();

        std::vector<TensorType> tbuf(n1 * n2);
        // cout << "s1,s2,n1,n2 = "  << s1 << "," << s2 <<
        //       "," << n1 <<"," << n2 <<endl;

        // compute shell pair; return is the pointer to the buffer
        engine.compute(shells[s1], shells[s2]);
        if(buf[0] == nullptr) continue;
        // "map" buffer to a const Eigen Matrix, and copy it to the
        // corresponding blocks of the result
        Eigen::Map<const Matrix> buf_mat(buf[0], n1, n2);
        Eigen::Map<Matrix>(&tbuf[0], n1, n2) = buf_mat;
        // tensor1e.put(blockid, tbuf);

        auto curshelloffset_i = 0U;
        auto curshelloffset_j = 0U;
        for(auto x = s1range_start; x < s1; x++) curshelloffset_i += AO_tiles[x];
        for(auto x = s2range_start; x < s2; x++) curshelloffset_j += AO_tiles[x];

        size_t c    = 0;
        auto   dimi = curshelloffset_i + AO_tiles[s1];
        auto   dimj = curshelloffset_j + AO_tiles[s2];

        // cout << "curshelloffset_i,curshelloffset_j,dimi,dimj = "  << curshelloffset_i << "," <<
        // curshelloffset_j <<
        //       "," << dimi <<"," << dimj <<endl;

        for(size_t i = curshelloffset_i; i < dimi; i++) {
          for(size_t j = curshelloffset_j; j < dimj; j++, c++) { dbuf[i * bd1 + j] = tbuf[c]; }
        }

        // if(s1!=s2){
        //     std::vector<TensorType> ttbuf(n1*n2);
        //     Eigen::Map<Matrix>(ttbuf.data(),n2,n1) = buf_mat.transpose();
        //     // Matrix buf_mat_trans = buf_mat.transpose();
        //     size_t c = 0;
        //     for(size_t j = curshelloffset_j; j < dimj; j++) {
        //       for(size_t i = curshelloffset_i; i < dimi; i++, c++) {
        //             dbuf[j*block_dims[0]+i] = ttbuf[c];
        //       }
        //     }
        // }
        // tensor1e.put({s2,s1}, ttbuf);
      }
    }
    tensor1e.put(blockid, dbuf);
  };

  block_for(ec, tensor1e(), compute_pchg_ints_lambda);
}

template<typename TensorType>
void exachem::scf::SCFGuess::scf_diagonalize(Scheduler& sch, ChemEnv& chem_env, SCFVars& scf_vars,
                                             ScalapackInfo& scalapack_info, TAMMTensors& ttensors,
                                             EigenTensors& etensors) {
  auto        rank     = sch.ec().pg().rank();
  SystemData& sys_data = chem_env.sys_data;
  // SCFOptions& scf_options = chem_env.ioptions.scf_options;
  // const bool debug      = scf_options.debug && rank==0;

  // solve F C = e S C by (conditioned) transformation to F' C' = e C',
  // where
  // F' = X.transpose() . F . X; the original C is obtained as C = X . C'

  // Eigen::SelfAdjointEigenSolver<Matrix> eig_solver_alpha(X_a.transpose() * F_alpha * X_a);
  // C_alpha = X_a * eig_solver_alpha.eigenvectors();
  // Eigen::SelfAdjointEigenSolver<Matrix> eig_solver_beta( X_b.transpose() * F_beta  * X_b);
  // C_beta  = X_b * eig_solver_beta.eigenvectors();

  const int64_t N      = sys_data.nbf_orig;
  const bool    is_uhf = sys_data.is_unrestricted;
  // const bool is_rhf = sys_data.is_restricted;
  const int nelectrons_alpha = sys_data.nelectrons_alpha;
  const int nelectrons_beta  = sys_data.nelectrons_beta;
  double    hl_gap           = 0;

#if defined(USE_SCALAPACK)
  if(scalapack_info.pg.is_valid()) {
    blacspp::Grid*                  blacs_grid       = scalapack_info.blacs_grid.get();
    scalapackpp::BlockCyclicDist2D* blockcyclic_dist = scalapack_info.blockcyclic_dist.get();

    auto desc_lambda = [&](const int64_t M, const int64_t N) {
      auto [M_loc, N_loc] = (*blockcyclic_dist).get_local_dims(M, N);
      return (*blockcyclic_dist).descinit_noerror(M, N, M_loc);
    };

    const auto& grid   = *blacs_grid;
    const auto  mb     = blockcyclic_dist->mb();
    const auto  Northo = sys_data.nbf;

    if(grid.ipr() >= 0 and grid.ipc() >= 0) {
      // TODO: Optimize intermediates here
      scalapackpp::BlockCyclicMatrix<double>
        // Fa_sca  ( grid, N,      N,      mb, mb ),
        // Xa_sca  ( grid, Northo, N,      mb, mb ), // Xa is row-major
        Fp_sca(grid, Northo, Northo, mb, mb), Ca_sca(grid, Northo, Northo, mb, mb),
        TMP1_sca(grid, N, Northo, mb, mb);

      auto desc_Fa = desc_lambda(N, N);
      auto desc_Xa = desc_lambda(Northo, N);

      tamm::to_block_cyclic_tensor(ttensors.F_alpha, ttensors.F_BC);
      scalapack_info.pg.barrier();

      auto Fa_tamm_lptr = ttensors.F_BC.access_local_buf();
      auto Xa_tamm_lptr = ttensors.X_alpha.access_local_buf();
      auto Ca_tamm_lptr = ttensors.C_alpha_BC.access_local_buf();

      // Compute TMP = F * X -> F * X**T (b/c row-major)
      // scalapackpp::pgemm( scalapackpp::Op::NoTrans, scalapackpp::Op::Trans,
      // 1., Fa_sca, Xa_sca, 0., TMP1_sca );

      scalapackpp::pgemm(scalapackpp::Op::NoTrans, scalapackpp::Op::Trans, TMP1_sca.m(),
                         TMP1_sca.n(), desc_Fa[3], 1., Fa_tamm_lptr, 1, 1, desc_Fa, Xa_tamm_lptr, 1,
                         1, desc_Xa, 0., TMP1_sca.data(), 1, 1, TMP1_sca.desc());

      // Compute Fp = X**T * TMP -> X * TMP (b/c row-major)
      // scalapackpp::pgemm( scalapackpp::Op::NoTrans, scalapackpp::Op::NoTrans,
      // 1., Xa_sca, TMP1_sca, 0., Fp_sca );

      scalapackpp::pgemm(scalapackpp::Op::NoTrans, scalapackpp::Op::NoTrans, Fp_sca.m(), Fp_sca.n(),
                         desc_Xa[3], 1., Xa_tamm_lptr, 1, 1, desc_Xa, TMP1_sca.data(), 1, 1,
                         TMP1_sca.desc(), 0., Fp_sca.data(), 1, 1, Fp_sca.desc());
      // Solve EVP
      etensors.eps_a.resize(Northo, 0.0);
      // scalapackpp::hereigd( scalapackpp::Job::Vec, scalapackpp::Uplo::Lower,
      //                       Fp_sca, etensors.eps_a.data(), Ca_sca );

#if defined(TAMM_USE_ELPA)
      elpa_t handle;
      int    error;

      // Initialize ELPA
      if(elpa_init(20221109) != ELPA_OK) tamm_terminate("ELPA API not supported");

      // Get and ELPA handle
      handle = elpa_allocate(&error);
      if(error != ELPA_OK) tamm_terminate("Could not create ELPA handle");

      auto [na_rows, na_cols] = (*blockcyclic_dist).get_local_dims(Northo, Northo);

      // Set parameters
      elpa_set(handle, "na", Northo, &error);
      elpa_set(handle, "nev", Northo, &error);
      elpa_set(handle, "local_nrows", (int) na_rows, &error);
      elpa_set(handle, "local_ncols", (int) na_cols, &error);
      elpa_set(handle, "nblk", (int) mb, &error);
      elpa_set(handle, "mpi_comm_parent", scalapack_info.pg.comm_c2f(), &error);
      elpa_set(handle, "process_row", (int) grid.ipr(), &error);
      elpa_set(handle, "process_col", (int) grid.ipc(), &error);
#if defined(USE_CUDA)
      elpa_set(handle, "nvidia-gpu", (int) 1, &error);
      // elpa_set(handle, "use_gpu_id", 1, &error);
#endif
      error = elpa_setup(handle);
      if(error != ELPA_OK) tamm_terminate(" ERROR: Could not setup ELPA");

      elpa_set(handle, "solver", ELPA_SOLVER_2STAGE, &error);

#if defined(USE_CUDA)
      elpa_set(handle, "real_kernel", ELPA_2STAGE_REAL_NVIDIA_GPU, &error);
#else
      elpa_set(handle, "real_kernel", ELPA_2STAGE_REAL_AVX2_BLOCK2, &error);
#endif

      // elpa_set(handle, "debug", 1, &error);
      // if (rank == 0 ) std::cout << " Calling ELPA " << std::endl;
      elpa_eigenvectors(handle, Fp_sca.data(), etensors.eps_a.data(), Ca_sca.data(), &error);
      if(error != ELPA_OK) tamm_terminate(" ERROR: ELPA Eigendecompoistion failed");

      // Clean-up
      elpa_deallocate(handle, &error);
      elpa_uninit(&error);
      if(error != ELPA_OK) tamm_terminate(" ERROR: ELPA deallocation failed");

#else
      /*info=*/scalapackpp::hereig(scalapackpp::Job::Vec, scalapackpp::Uplo::Lower, Fp_sca.m(),
                                   Fp_sca.data(), 1, 1, Fp_sca.desc(), etensors.eps_a.data(),
                                   Ca_sca.data(), 1, 1, Ca_sca.desc());
#endif

      // Backtransform TMP = X * Ca -> TMP**T = Ca**T * X
      // scalapackpp::pgemm( scalapackpp::Op::Trans, scalapackpp::Op::NoTrans,
      //                     1., Ca_sca, Xa_sca, 0., TMP2_sca );
      scalapackpp::pgemm(scalapackpp::Op::Trans, scalapackpp::Op::NoTrans, desc_Xa[2], desc_Xa[3],
                         Ca_sca.m(), 1., Ca_sca.data(), 1, 1, Ca_sca.desc(), Xa_tamm_lptr, 1, 1,
                         desc_Xa, 0., Ca_tamm_lptr, 1, 1, desc_Xa);

      if(!scf_vars.lshift_reset)
        hl_gap = etensors.eps_a[nelectrons_alpha] - etensors.eps_a[nelectrons_alpha - 1];

      // Gather results
      // if(scalapack_info.pg.rank() == 0) C_alpha.resize(N, Northo);
      // TMP2_sca.gather_from(Northo, N, C_alpha.data(), Northo, 0, 0);

      if(is_uhf) {
        tamm::to_block_cyclic_tensor(ttensors.F_beta, ttensors.F_BC);
        scalapack_info.pg.barrier();
        Fa_tamm_lptr = ttensors.F_BC.access_local_buf();
        Xa_tamm_lptr = ttensors.X_alpha.access_local_buf();
        Ca_tamm_lptr = ttensors.C_beta_BC.access_local_buf();

        // Compute TMP = F * X -> F * X**T (b/c row-major)
        // scalapackpp::pgemm( scalapackpp::Op::NoTrans, scalapackpp::Op::Trans,
        //                     1., Fa_sca, Xa_sca, 0., TMP1_sca );
        scalapackpp::pgemm(scalapackpp::Op::NoTrans, scalapackpp::Op::Trans, TMP1_sca.m(),
                           TMP1_sca.n(), desc_Fa[3], 1., Fa_tamm_lptr, 1, 1, desc_Fa, Xa_tamm_lptr,
                           1, 1, desc_Xa, 0., TMP1_sca.data(), 1, 1, TMP1_sca.desc());

        // Compute Fp = X**T * TMP -> X * TMP (b/c row-major)
        // scalapackpp::pgemm( scalapackpp::Op::NoTrans, scalapackpp::Op::NoTrans,
        //                     1., Xa_sca, TMP1_sca, 0., Fp_sca );
        scalapackpp::pgemm(scalapackpp::Op::NoTrans, scalapackpp::Op::NoTrans, Fp_sca.m(),
                           Fp_sca.n(), desc_Xa[3], 1., Xa_tamm_lptr, 1, 1, desc_Xa, TMP1_sca.data(),
                           1, 1, TMP1_sca.desc(), 0., Fp_sca.data(), 1, 1, Fp_sca.desc());

        // Solve EVP
        etensors.eps_b.resize(Northo, 0.0);
        // scalapackpp::hereigd( scalapackpp::Job::Vec, scalapackpp::Uplo::Lower,
        //                       Fp_sca, etensors.eps_b.data(), Ca_sca );
#if defined(TAMM_USE_ELPA)
        elpa_t handle;
        int    error;

        // Initialize ELPA
        if(elpa_init(20221109) != ELPA_OK) tamm_terminate("ELPA API not supported");

        // Get and ELPA handle
        handle = elpa_allocate(&error);
        if(error != ELPA_OK) tamm_terminate("Could not create ELPA handle");

        auto [na_rows, na_cols] = (*blockcyclic_dist).get_local_dims(Northo, Northo);

        // Set parameters
        elpa_set(handle, "na", Northo, &error);
        elpa_set(handle, "nev", Northo, &error);
        elpa_set(handle, "local_nrows", (int) na_rows, &error);
        elpa_set(handle, "local_ncols", (int) na_cols, &error);
        elpa_set(handle, "nblk", (int) mb, &error);
        elpa_set(handle, "mpi_comm_parent", scalapack_info.pg.comm_c2f(), &error);
        elpa_set(handle, "process_row", (int) grid.ipr(), &error);
        elpa_set(handle, "process_col", (int) grid.ipc(), &error);
#if defined(USE_CUDA)
        elpa_set(handle, "nvidia-gpu", (int) 1, &error);
        // elpa_set(handle, "use_gpu_id", 1, &error);
#endif
        error = elpa_setup(handle);
        if(error != ELPA_OK) tamm_terminate(" ERROR: Could not setup ELPA");

        elpa_set(handle, "solver", ELPA_SOLVER_2STAGE, &error);
#if defined(USE_CUDA)
        elpa_set(handle, "real_kernel", ELPA_2STAGE_REAL_NVIDIA_GPU, &error);
#else
        elpa_set(handle, "real_kernel", ELPA_2STAGE_REAL_AVX2_BLOCK2, &error);
#endif
        // elpa_set(handle, "debug", 1, &error);
        // if (rank == 0 ) std::cout << " Calling ELPA " << std::endl;
        elpa_eigenvectors(handle, Fp_sca.data(), etensors.eps_b.data(), Ca_sca.data(), &error);
        if(error != ELPA_OK) tamm_terminate(" ERROR: ELPA Eigendecompoistion failed");

        // Clean-up
        elpa_deallocate(handle, &error);
        elpa_uninit(&error);
        if(error != ELPA_OK) tamm_terminate(" ERROR: ELPA deallocation failed");

#else
        /*info=*/scalapackpp::hereig(scalapackpp::Job::Vec, scalapackpp::Uplo::Lower, Fp_sca.m(),
                                     Fp_sca.data(), 1, 1, Fp_sca.desc(), etensors.eps_b.data(),
                                     Ca_sca.data(), 1, 1, Ca_sca.desc());
#endif
        // Backtransform TMP = X * Cb -> TMP**T = Cb**T * X
        // scalapackpp::pgemm( scalapackpp::Op::Trans, scalapackpp::Op::NoTrans,
        //                     1., Ca_sca, Xa_sca, 0., TMP2_sca );
        scalapackpp::pgemm(scalapackpp::Op::Trans, scalapackpp::Op::NoTrans, desc_Xa[2], desc_Xa[3],
                           Ca_sca.m(), 1., Ca_sca.data(), 1, 1, Ca_sca.desc(), Xa_tamm_lptr, 1, 1,
                           desc_Xa, 0., Ca_tamm_lptr, 1, 1, desc_Xa);

        // Gather results
        // if(scalapack_info.pg.rank() == 0) C_beta.resize(N, Northo);
        // TMP2_sca.gather_from(Northo, N, C_beta.data(), Northo, 0, 0);

        if(!scf_vars.lshift_reset)
          hl_gap =
            std::min(hl_gap, etensors.eps_b[nelectrons_beta] - etensors.eps_b[nelectrons_beta - 1]);
      }
    } // rank participates in ScaLAPACK call
  }
  sch.ec().pg().barrier();

#else

  Matrix& C_alpha = etensors.C_alpha;
  Matrix& C_beta  = etensors.C_beta;

  const int64_t Northo_a = sys_data.nbf; // X_a.cols();
  // TODO: avoid eigen Fp
  Matrix X_a;
  if(rank == 0) {
    // alpha
    Matrix Fp = tamm_to_eigen_matrix(ttensors.F_alpha);
    X_a       = tamm_to_eigen_matrix(ttensors.X_alpha);
    C_alpha.resize(N, Northo_a);
    etensors.eps_a.resize(Northo_a, 0.0);

    blas::gemm(blas::Layout::ColMajor, blas::Op::NoTrans, blas::Op::Trans, N, Northo_a, N, 1.,
               Fp.data(), N, X_a.data(), Northo_a, 0., C_alpha.data(), N);
    blas::gemm(blas::Layout::ColMajor, blas::Op::NoTrans, blas::Op::NoTrans, Northo_a, Northo_a, N,
               1., X_a.data(), Northo_a, C_alpha.data(), N, 0., Fp.data(), Northo_a);
    lapack::syevd(lapack::Job::Vec, lapack::Uplo::Lower, Northo_a, Fp.data(), Northo_a,
                  etensors.eps_a.data());
    blas::gemm(blas::Layout::ColMajor, blas::Op::Trans, blas::Op::NoTrans, Northo_a, N, Northo_a,
               1., Fp.data(), Northo_a, X_a.data(), Northo_a, 0., C_alpha.data(), Northo_a);
    if(!scf_vars.lshift_reset)
      hl_gap = etensors.eps_a[nelectrons_alpha] - etensors.eps_a[nelectrons_alpha - 1];
  }

  if(is_uhf) {
    const int64_t Northo_b = sys_data.nbf; // X_b.cols();
    if(rank == 0) {
      // beta
      Matrix Fp = tamm_to_eigen_matrix(ttensors.F_beta);
      C_beta.resize(N, Northo_b);
      etensors.eps_b.resize(Northo_b, 0.0);
      Matrix& X_b = X_a;

      blas::gemm(blas::Layout::ColMajor, blas::Op::NoTrans, blas::Op::Trans, N, Northo_b, N, 1.,
                 Fp.data(), N, X_b.data(), Northo_b, 0., C_beta.data(), N);
      blas::gemm(blas::Layout::ColMajor, blas::Op::NoTrans, blas::Op::NoTrans, Northo_b, Northo_b,
                 N, 1., X_b.data(), Northo_b, C_beta.data(), N, 0., Fp.data(), Northo_b);
      lapack::syevd(lapack::Job::Vec, lapack::Uplo::Lower, Northo_b, Fp.data(), Northo_b,
                    etensors.eps_b.data());
      blas::gemm(blas::Layout::ColMajor, blas::Op::Trans, blas::Op::NoTrans, Northo_b, N, Northo_b,
                 1., Fp.data(), Northo_b, X_b.data(), Northo_b, 0., C_beta.data(), Northo_b);

      if(!scf_vars.lshift_reset)
        hl_gap =
          std::min(hl_gap, etensors.eps_b[nelectrons_beta] - etensors.eps_b[nelectrons_beta - 1]);
    }
  }
#endif

  // Remove the level-shift from the hl_gap
  hl_gap -= scf_vars.lshift;

  if(!scf_vars.lshift_reset) {
    sch.ec().pg().broadcast(&hl_gap, 0);
    if(hl_gap < 1e-2 && !(chem_env.ioptions.scf_options.lshift > 0)) {
      scf_vars.lshift_reset = true;
      scf_vars.lshift       = 0.5;
      if(rank == 0) cout << "Resetting lshift to " << scf_vars.lshift << endl;
    }
  }
}

template<typename TensorType>
void exachem::scf::SCFGuess::compute_sad_guess(ExecutionContext& ec, ChemEnv& chem_env,
                                               SCFVars& scf_vars, ScalapackInfo& scalapack_info,
                                               EigenTensors& etensors, TAMMTensors& ttensors) {
  auto ig1 = std::chrono::high_resolution_clock::now();

  // bool is_spherical{true};
  SystemData&                       sys_data    = chem_env.sys_data;
  SCFOptions&                       scf_options = chem_env.ioptions.scf_options;
  std::vector<ECAtom>&              ec_atoms    = chem_env.ec_atoms;
  const std::vector<libint2::Atom>& atoms       = chem_env.atoms;

  const libint2::BasisSet& shells_tot = chem_env.shells;
  // const std::string& basis = scf_options.basis;
  // int charge = scf_options.charge;
  // int multiplicity = scf_options.multiplicity;

  Scheduler  sch{ec};
  const bool is_uhf = sys_data.is_unrestricted;
  const bool is_rhf = sys_data.is_restricted;

  // int neutral_charge = sys_data.nelectrons + charge;
  // double N_to_Neu  = (double)sys_data.nelectrons/neutral_charge;
  // double Na_to_Neu = (double) sys_data.nelectrons_alpha / neutral_charge;
  // double Nb_to_Neu = (double) sys_data.nelectrons_beta / neutral_charge;

  /*
  Superposition of Atomic Density
  */

  const auto rank = ec.pg().rank();
  size_t     N    = shells_tot.nbf();

  // D,G,D_b are only allocated on rank 0 for SAD when DF-HF is enabled
  if((scf_vars.do_dens_fit && !scf_vars.direct_df) && rank == 0) {
    etensors.D_alpha = Matrix::Zero(N, N);
    etensors.G_alpha = Matrix::Zero(N, N);
    if(is_uhf) etensors.D_beta = Matrix::Zero(N, N);
  }

  Matrix& D_tot_a = etensors.D_alpha;
  Matrix& D_tot_b = etensors.G_alpha;

  // Get atomic occupations

  std::vector<libint2::Atom> atoms_copy;
  for(auto& k: ec_atoms) { atoms_copy.push_back(k.atom); }
  auto occs = compute_soad(atoms_copy);

  double fock_precision = std::numeric_limits<double>::epsilon();
  // auto   scf_options    = scf_options;

  // loop over atoms
  size_t indx  = 0;
  size_t iatom = 0;

  std::map<string, int> atom_loc;

  for(const auto& k: ec_atoms) {
    // const auto Z = k.atom.atomic_number;
    const auto        es             = k.esymbol;
    const bool        has_ecp        = k.has_ecp;
    auto              acharge        = scf_options.charge;
    auto              amultiplicity  = scf_options.multiplicity;
    bool              custom_opts    = false;
    bool              spin_polarized = false;
    bool              do_charges     = false;
    std::vector<Atom> atom;
    auto              kcopy = k.atom;
    atom.push_back(kcopy);

    // Generate local basis set
    libint2::BasisSet shells_atom(k.basis, atom);
    shells_atom.set_pure(true);
    size_t nao_atom = shells_atom.nbf();

    if(atom_loc.find(es) != atom_loc.end()) {
      int atom_indx = atom_loc[es];

      if(rank == 0) {
        D_tot_a.block(indx, indx, nao_atom, nao_atom) =
          D_tot_a.block(atom_indx, atom_indx, nao_atom, nao_atom);
        D_tot_b.block(indx, indx, nao_atom, nao_atom) =
          D_tot_b.block(atom_indx, atom_indx, nao_atom, nao_atom);
      }

      // atom_loc[es] = indx;
      indx += nao_atom;
      ++iatom;

      atom.pop_back();
      continue;
    }

    // Modify occupations if ecp is present
    if(has_ecp) {
      int ncore = k.ecp_nelec;

      // Obtain the type of ECP depending on ncore
      size_t index = std::distance(nelecp.begin(), std::find(nelecp.begin(), nelecp.end(), ncore));
      if(index > nelecp.size()) tamm_terminate("Error: ECP type not compatiable");

      // Start removing electrons according to occupations
      for(size_t i = 0; i < occecp[iecp[index]].size(); ++i) {
        const int l = occecp[iecp[index]][i];
        occs(iatom, l) -= 2.0 * (2.0 * l + 1.0);
        ncore -= 2 * (2 * l + 1);
        if(ncore < 1) break;
      }
    }

    // Check if user supplied custom options
    auto guess_atom_options = scf_options.guess_atom_options;
    std::vector<std::pair<double, std::array<double, 3>>> q;
    if(guess_atom_options.find(es) != guess_atom_options.end()) {
      std::tie(acharge, amultiplicity) = guess_atom_options[es];
      custom_opts                      = true;
      // Use point charges to lift degeneracies
      for(size_t j = 0; j < atoms.size(); ++j) {
        if(j == iatom) continue;
        do_charges = true;
        q.push_back({0.05, {{atoms[j].x, atoms[j].y, atoms[j].z}}});
      }
    }

    int nelectrons_alpha_atom;
    int nelectrons_beta_atom;
    if(custom_opts) {
      int nelectrons = k.atom.atomic_number - acharge;
      if(has_ecp) nelectrons -= k.ecp_nelec;
      nelectrons_alpha_atom = (nelectrons + amultiplicity - 1) / 2;
      nelectrons_beta_atom  = nelectrons - nelectrons_alpha_atom;
    }

    auto       s2bf_atom = shells_atom.shell2bf();
    SCFCompute scf_compute;
    std::tie(scf_vars.obs_shellpair_list_atom, scf_vars.obs_shellpair_data_atom) =
      scf_compute.compute_shellpairs(shells_atom);
    // if(rank == 0) cout << "compute shell pairs for present basis" << endl;

    // Get occupations
    Eigen::Vector<EigenTensorType, 4> occ_atom_b = {0.0, 0.0, 0.0, 0.0};
    Eigen::Vector<EigenTensorType, 4> occ_atom_a = {0.0, 0.0, 0.0, 0.0};
    for(int l = 0; l < 4; ++l) {
      const double norb = 2.0 * l + 1.0;                      // Number of orbitals
      const int    ndbl = occs(iatom, l) / (2 * (2 * l + 1)); // Number of doubly occupied orbitals
      occ_atom_a(l)     = ndbl * norb + std::min(occs(iatom, l) - 2 * ndbl * norb, norb);
      occ_atom_b(l)     = ndbl * norb + std::max(occs(iatom, l) - occ_atom_a(l) - ndbl * norb, 0.0);
    }
    Eigen::Vector<EigenTensorType, 4> _occ_atom_a = occ_atom_a;
    Eigen::Vector<EigenTensorType, 4> _occ_atom_b = occ_atom_b;

    // Generate Density Matrix Guess
    Matrix D_a_atom = Matrix::Zero(nao_atom, nao_atom);
    Matrix D_b_atom = Matrix::Zero(nao_atom, nao_atom);
    for(size_t ishell = 0; ishell < shells_atom.size(); ++ishell) {
      const int    l    = shells_atom[ishell].contr[0].l;
      const double norb = 2.0 * l + 1.0;

      if(l > 3) continue;                // No atom has electrons in shells with l > 3
      if(_occ_atom_a(l) < 0.1) continue; // No more electrons to be added

      double nocc_a = std::min(_occ_atom_a(l) / norb, 1.0);
      double nocc_b = std::min(_occ_atom_b(l) / norb, 1.0);
      _occ_atom_a(l) -= nocc_a * norb;
      _occ_atom_b(l) -= nocc_b * norb;

      int bf1 = s2bf_atom[ishell];
      int bf2 = bf1 + 2 * l;
      for(int ibf = bf1; ibf <= bf2; ++ibf) {
        D_a_atom(ibf, ibf) = nocc_a;
        D_b_atom(ibf, ibf) = nocc_b;
      }
    }

    if(!spin_polarized) {
      D_a_atom = 0.5 * (D_a_atom + D_b_atom);
      D_b_atom = D_a_atom;
    }

    // cout << endl << iatom << endl << D_a_atom << endl;

    tamm::Tile tile_size_atom = scf_options.AO_tilesize;

    if(tile_size_atom < nao_atom * 0.05) tile_size_atom = std::ceil(nao_atom * 0.05);

    std::vector<Tile> AO_tiles_atom;
    for(auto s: shells_atom) AO_tiles_atom.push_back(s.size());

    tamm::Tile          est_ts_atom = 0;
    std::vector<Tile>   AO_opttiles_atom;
    std::vector<size_t> shell_tile_map_atom;
    for(auto s = 0U; s < shells_atom.size(); s++) {
      est_ts_atom += shells_atom[s].size();
      if(est_ts_atom >= tile_size_atom) {
        AO_opttiles_atom.push_back(est_ts_atom);
        shell_tile_map_atom.push_back(s); // shell id specifying tile boundary
        est_ts_atom = 0;
      }
    }
    if(est_ts_atom > 0) {
      AO_opttiles_atom.push_back(est_ts_atom);
      shell_tile_map_atom.push_back(shells_atom.size() - 1);
    }

    // if(rank == 0) cout << "compute tile info for present basis" << endl;

    IndexSpace            AO_atom{range(0, nao_atom)};
    tamm::TiledIndexSpace tAO_atom, tAOt_atom;
    tAO_atom  = {AO_atom, AO_opttiles_atom};
    tAOt_atom = {AO_atom, AO_tiles_atom};

    // compute core hamiltonian H and overlap S for the atom
    Tensor<TensorType> H_atom{tAO_atom, tAO_atom};
    Tensor<TensorType> S_atom{tAO_atom, tAO_atom};
    Tensor<TensorType> T_atom{tAO_atom, tAO_atom};
    Tensor<TensorType> V_atom{tAO_atom, tAO_atom};
    Tensor<TensorType> Q_atom{tAO_atom, tAO_atom};
    Tensor<TensorType> E_atom{tAO_atom, tAO_atom};
    Tensor<TensorType>::allocate(&ec, H_atom, S_atom, T_atom, V_atom, Q_atom, E_atom);
    Matrix H_atom_eig = Matrix::Zero(nao_atom, nao_atom);
    Matrix S_atom_eig = Matrix::Zero(nao_atom, nao_atom);

    using libint2::Operator;

    TiledIndexSpace           tAO            = scf_vars.tAO;
    const std::vector<Tile>   AO_tiles       = scf_vars.AO_tiles;
    const std::vector<size_t> shell_tile_map = scf_vars.shell_tile_map;

    scf_vars.tAO            = tAO_atom;
    scf_vars.AO_tiles       = AO_tiles_atom;
    scf_vars.shell_tile_map = shell_tile_map_atom;

    std::vector<libecpint::ECP>           ecps;
    std::vector<libecpint::GaussianShell> libecp_shells;
    if(has_ecp) {
      for(auto shell: shells_atom) {
        std::array<double, 3>    O = {shell.O[0], shell.O[1], shell.O[2]};
        libecpint::GaussianShell newshell(O, shell.contr[0].l);
        for(size_t iprim = 0; iprim < shell.alpha.size(); iprim++)
          newshell.addPrim(shell.alpha[iprim], shell.contr[0].coeff[iprim]);
        libecp_shells.push_back(newshell);
      }

      int  maxam   = *std::max_element(k.ecp_ams.begin(), k.ecp_ams.end());
      auto ecp_ams = k.ecp_ams;
      std::replace(ecp_ams.begin(), ecp_ams.end(), -1, maxam + 1);
      std::array<double, 3> O = {atom[0].x, atom[0].y, atom[0].z};
      // std::cout << O << std::endl;
      libecpint::ECP newecp(O.data());
      for(size_t iprim = 0; iprim < k.ecp_coeffs.size(); iprim++)
        newecp.addPrimitive(k.ecp_ns[iprim], ecp_ams[iprim], k.ecp_exps[iprim], k.ecp_coeffs[iprim],
                            true);
      // std::cout << "noType1: " << newecp.noType1() << std::endl;
      // std::cout << "U_l(r):  " << newecp.evaluate(0.1, 2) << std::endl;
      ecps.push_back(newecp);
    }

    compute_1body_ints(ec, scf_vars, S_atom, atom, shells_atom, Operator::overlap);
    compute_1body_ints(ec, scf_vars, T_atom, atom, shells_atom, Operator::kinetic);
    if(has_ecp) atom[0].atomic_number -= k.ecp_nelec;
    compute_1body_ints(ec, scf_vars, V_atom, atom, shells_atom, Operator::nuclear);

    if(custom_opts && do_charges) {
      compute_pchg_ints(ec, scf_vars, Q_atom, q, shells_atom, Operator::nuclear);
    }
    else { Scheduler{ec}(Q_atom() = 0.0).execute(); }

    if(has_ecp) { compute_ecp_ints<TensorType>(ec, scf_vars, E_atom, libecp_shells, ecps); }
    else { Scheduler{ec}(E_atom() = 0.0).execute(); }

    // if(rank == 0) cout << "compute one body ints" << endl;

    // clang-format off
    Scheduler{ec}
      (H_atom()  = T_atom())
      (H_atom() += V_atom())
      (H_atom() += Q_atom())
      (H_atom() += E_atom())
      .deallocate(T_atom, V_atom, Q_atom, E_atom)
      .execute();
    // clang-format on

    // if(rank == 0) cout << "compute H_atom" << endl;

    t2e_hf_helper<TensorType, 2>(ec, H_atom, H_atom_eig, "H1-H-atom");
    t2e_hf_helper<TensorType, 2>(ec, S_atom, S_atom_eig, "S1-S-atom");

    // if(rank == 0) cout << std::setprecision(6) << "H_atom: " << endl << H_atom_eig << endl
    //                                            << "S_atom: " << endl << S_atom_eig << endl;

    // Form X_atom
    size_t obs_rank;
    double S_condition_number;
    double XtX_condition_number;
    double S_condition_number_threshold = scf_options.tol_lindep;

    assert(S_atom_eig.rows() == S_atom_eig.cols());

    // std::tie(X_atom, Xinv, obs_rank, S_condition_number, XtX_condition_number, n_illcond) =
    //     gensqrtinv(ec, S_atom_eig, false, S_condition_number_threshold);

    Matrix X_atom;
    std::tie(X_atom, obs_rank, S_condition_number, XtX_condition_number) =
      gensqrtinv_atscf(ec, chem_env, scf_vars, scalapack_info, S_atom, tAO_atom, false,
                       S_condition_number_threshold);

    // if(rank == 0) cout << std::setprecision(6) << "X_atom: " << endl << X_atom << endl;

    // Projecting minimal basis SOAD onto basis set specified
    // double          precision = std::numeric_limits<double>::epsilon();
    const libint2::BasisSet& obs = shells_atom;

    Matrix             G_a_atom = Matrix::Zero(nao_atom, nao_atom);
    Matrix             G_b_atom = Matrix::Zero(nao_atom, nao_atom);
    Tensor<TensorType> F1tmp_atom{tAOt_atom, tAOt_atom}; // not allocated

    using libint2::BraKet;
    using libint2::Engine;
    using libint2::Operator;

    auto shell2bf = obs.shell2bf();

    // Form intial guess of Fock matrix and Density matrix for the present basis
    auto Ft_a_atom = H_atom_eig;
    auto Ft_b_atom = H_atom_eig;

    // if(rank == 0) cout << std::setprecision(6) << "Ft_a_atom: " << endl << Ft_a_atom << endl;

    Matrix C_a_atom = Matrix::Zero(nao_atom, nao_atom);
    Matrix C_b_atom = Matrix::Zero(nao_atom, nao_atom);

    // if(rank == 0) cout << std::setprecision(6) << "D_a_atom: " << endl << D_a_atom << endl;

    // Atomic SCF loop
    double rmsd_a_atom = 1.0;
    // double     rmsd_b_atom       = 1.0;
    int        iter_atom         = 0;
    Matrix     D_a_atom_last     = Matrix::Zero(nao_atom, nao_atom);
    Matrix     D_b_atom_last     = Matrix::Zero(nao_atom, nao_atom);
    auto       SchwarzK          = scf_compute.compute_schwarz_ints<>(ec, scf_vars, shells_atom);
    const auto do_schwarz_screen = SchwarzK.cols() != 0 && SchwarzK.rows() != 0;

    scf_vars.tAO            = tAO;
    scf_vars.AO_tiles       = AO_tiles;
    scf_vars.shell_tile_map = shell_tile_map;

    using libint2::Engine;
    Engine engine(Operator::coulomb, obs.max_nprim(), obs.max_l(), 0);
    engine.set_precision(0.0);
    const auto& buf = engine.results();

    Tensor<TensorType> F1tmp_atom2{tAOt_atom, tAOt_atom}; // not allocated
    Tensor<TensorType> F1tmp1_a_atom2{tAO_atom, tAO_atom};
    Tensor<TensorType> F1tmp1_b_atom2{tAO_atom, tAO_atom};
    Tensor<TensorType>::allocate(&ec, F1tmp1_a_atom2, F1tmp1_b_atom2);

    do {
      ++iter_atom;
      D_a_atom_last            = D_a_atom;
      D_b_atom_last            = D_b_atom;
      Matrix D_shblk_norm_atom = chem_env.compute_shellblock_norm(obs, D_a_atom);
      D_shblk_norm_atom += chem_env.compute_shellblock_norm(obs, D_b_atom);

      Matrix G_a_atom2 = Matrix::Zero(nao_atom, nao_atom);
      Matrix G_b_atom2 = Matrix::Zero(nao_atom, nao_atom);

      auto comp_2bf_lambda_atom = [&](IndexVector blockid) {
        auto s1        = blockid[0];
        auto bf1_first = shell2bf[s1];
        auto n1        = obs[s1].size();
        auto sp12_iter = scf_vars.obs_shellpair_data_atom.at(s1).begin();

        auto s2     = blockid[1];
        auto s2spl  = scf_vars.obs_shellpair_list_atom.at(s1);
        auto s2_itr = std::find(s2spl.begin(), s2spl.end(), s2);
        if(s2_itr == s2spl.end()) return;
        auto s2_pos    = std::distance(s2spl.begin(), s2_itr);
        auto bf2_first = shell2bf[s2];
        auto n2        = obs[s2].size();
        bool do12      = obs[s1].contr[0].l == obs[s2].contr[0].l;

        std::advance(sp12_iter, s2_pos);
        const auto* sp12 = sp12_iter->get();

        const auto Dnorm12 = do_schwarz_screen ? D_shblk_norm_atom(s1, s2) : 0.;

        for(decltype(s1) s3 = 0; s3 <= s1; ++s3) {
          auto bf3_first = shell2bf[s3];
          auto n3        = obs[s3].size();
          bool do13      = obs[s1].contr[0].l == obs[s3].contr[0].l;
          bool do23      = obs[s2].contr[0].l == obs[s3].contr[0].l;

          const auto Dnorm123 =
            do_schwarz_screen
              ? std::max(D_shblk_norm_atom(s1, s3), std::max(D_shblk_norm_atom(s2, s3), Dnorm12))
              : 0.;

          auto sp34_iter = scf_vars.obs_shellpair_data_atom.at(s3).begin();

          const auto s4_max = (s1 == s3) ? s2 : s3;
          for(const auto& s4: scf_vars.obs_shellpair_list_atom.at(s3)) {
            if(s4 > s4_max) break;
            bool do14 = obs[s1].contr[0].l == obs[s4].contr[0].l;
            bool do24 = obs[s2].contr[0].l == obs[s4].contr[0].l;
            bool do34 = obs[s3].contr[0].l == obs[s4].contr[0].l;

            const auto* sp34 = sp34_iter->get();
            ++sp34_iter;

            if(not(do12 or do34 or (do13 and do24) or (do14 and do23))) continue;

            const auto Dnorm1234 =
              do_schwarz_screen ? std::max(D_shblk_norm_atom(s1, s4),
                                           std::max(D_shblk_norm_atom(s2, s4),
                                                    std::max(D_shblk_norm_atom(s3, s4), Dnorm123)))
                                : 0.;

            if(do_schwarz_screen &&
               Dnorm1234 * SchwarzK(s1, s2) * SchwarzK(s3, s4) < fock_precision)
              continue;

            auto bf4_first = shell2bf[s4];
            auto n4        = obs[s4].size();

            auto s12_deg    = (s1 == s2) ? 1 : 2;
            auto s34_deg    = (s3 == s4) ? 1 : 2;
            auto s12_34_deg = (s1 == s3) ? (s2 == s4 ? 1 : 2) : 2;
            auto s1234_deg  = s12_deg * s34_deg * s12_34_deg;

            engine.compute2<Operator::coulomb, libint2::BraKet::xx_xx, 0>(obs[s1], obs[s2], obs[s3],
                                                                          obs[s4], sp12, sp34);

            const auto* buf_1234 = buf[0];
            if(buf_1234 == nullptr) continue; // if all integrals screened out, skip to next quartet

            if(do12 or do34) {
              for(decltype(n1) f1 = 0, f1234 = 0; f1 != n1; ++f1) {
                const auto bf1 = f1 + bf1_first;
                for(decltype(n2) f2 = 0; f2 != n2; ++f2) {
                  const auto bf2 = f2 + bf2_first;
                  for(decltype(n3) f3 = 0; f3 != n3; ++f3) {
                    const auto bf3 = f3 + bf3_first;
                    for(decltype(n4) f4 = 0; f4 != n4; ++f4, ++f1234) {
                      const auto bf4               = f4 + bf4_first;
                      const auto value             = buf_1234[f1234];
                      const auto value_scal_by_deg = value * s1234_deg;
                      const auto g12 =
                        0.5 * (D_a_atom(bf3, bf4) + D_b_atom(bf3, bf4)) * value_scal_by_deg;
                      const auto g34 =
                        0.5 * (D_a_atom(bf1, bf2) + D_b_atom(bf1, bf2)) * value_scal_by_deg;
                      // alpha_part
                      G_a_atom2(bf1, bf2) += g12;
                      G_a_atom2(bf3, bf4) += g34;

                      // beta_part
                      G_b_atom2(bf1, bf2) += g12;
                      G_b_atom2(bf3, bf4) += g34;
                    }
                  }
                }
              }
            }

            if((do13 and do24) or (do14 and do23)) {
              for(decltype(n1) f1 = 0, f1234 = 0; f1 != n1; ++f1) {
                const auto bf1 = f1 + bf1_first;
                for(decltype(n2) f2 = 0; f2 != n2; ++f2) {
                  const auto bf2 = f2 + bf2_first;
                  for(decltype(n3) f3 = 0; f3 != n3; ++f3) {
                    const auto bf3 = f3 + bf3_first;
                    for(decltype(n4) f4 = 0; f4 != n4; ++f4, ++f1234) {
                      const auto bf4               = f4 + bf4_first;
                      const auto value             = buf_1234[f1234];
                      const auto value_scal_by_deg = value * s1234_deg;
                      // alpha_part
                      G_a_atom2(bf2, bf3) -= 0.25 * D_a_atom(bf1, bf4) * value_scal_by_deg;
                      G_a_atom2(bf2, bf4) -= 0.25 * D_a_atom(bf1, bf3) * value_scal_by_deg;
                      G_a_atom2(bf1, bf3) -= 0.25 * D_a_atom(bf2, bf4) * value_scal_by_deg;
                      G_a_atom2(bf1, bf4) -= 0.25 * D_a_atom(bf2, bf3) * value_scal_by_deg;

                      // beta_part
                      G_b_atom2(bf1, bf3) -= 0.25 * D_b_atom(bf2, bf4) * value_scal_by_deg;
                      G_b_atom2(bf1, bf4) -= 0.25 * D_b_atom(bf2, bf3) * value_scal_by_deg;
                      G_b_atom2(bf2, bf3) -= 0.25 * D_b_atom(bf1, bf4) * value_scal_by_deg;
                      G_b_atom2(bf2, bf4) -= 0.25 * D_b_atom(bf1, bf3) * value_scal_by_deg;
                    }
                  }
                }
              }
            }
          }
        }
      };

      block_for(ec, F1tmp_atom2(), comp_2bf_lambda_atom);
      // symmetrize G
      Matrix Gt_a_atom2 = 0.5 * (G_a_atom2 + G_a_atom2.transpose());
      G_a_atom2         = Gt_a_atom2;
      Gt_a_atom2.resize(0, 0);
      Matrix Gt_b_atom2 = 0.5 * (G_b_atom2 + G_b_atom2.transpose());
      G_b_atom2         = Gt_b_atom2;
      Gt_b_atom2.resize(0, 0);

      sch(F1tmp1_a_atom2() = 0.0)(F1tmp1_b_atom2() = 0.0).execute();

      eigen_to_tamm_tensor_acc(F1tmp1_a_atom2, G_a_atom2);
      eigen_to_tamm_tensor_acc(F1tmp1_b_atom2, G_b_atom2);
      ec.pg().barrier();

      Matrix F_a_atom = Matrix::Zero(nao_atom, nao_atom);
      tamm_to_eigen_tensor(F1tmp1_a_atom2, F_a_atom);
      F_a_atom += H_atom_eig;
      if(iter_atom > 1) F_a_atom -= 0.05 * S_atom_eig * D_a_atom * S_atom_eig;
      Eigen::SelfAdjointEigenSolver<Matrix> eig_solver_alpha(X_atom.transpose() * F_a_atom *
                                                             X_atom);
      C_a_atom = X_atom * eig_solver_alpha.eigenvectors();
      D_a_atom = Matrix::Zero(nao_atom, nao_atom);

      if(spin_polarized || custom_opts) {
        Matrix F_b_atom = Matrix::Zero(nao_atom, nao_atom);
        tamm_to_eigen_tensor(F1tmp1_b_atom2, F_b_atom);
        F_b_atom += H_atom_eig;
        if(iter_atom > 1) F_b_atom -= 0.05 * S_atom_eig * D_b_atom * S_atom_eig;
        Eigen::SelfAdjointEigenSolver<Matrix> eig_solver_beta(X_atom.transpose() * F_b_atom *
                                                              X_atom);
        C_b_atom = X_atom * eig_solver_beta.eigenvectors();
      }
      else { C_b_atom = C_a_atom; }
      D_b_atom = Matrix::Zero(nao_atom, nao_atom);

      // Use atomic occupations to get density
      // int    iocc = 0;
      // int    ideg = 0;
      // bool   deg  = false;

      Matrix occvec(nao_atom, 1);
      occvec.setZero();

      if(custom_opts) {
        D_a_atom = C_a_atom.leftCols(nelectrons_alpha_atom) *
                   C_a_atom.leftCols(nelectrons_alpha_atom).transpose();
        D_b_atom = C_b_atom.leftCols(nelectrons_beta_atom) *
                   C_b_atom.leftCols(nelectrons_beta_atom).transpose();
      }
      else {
        _occ_atom_a = occ_atom_a;
        _occ_atom_b = occ_atom_b;
        // Alpha
        for(size_t imo = 0; imo < nao_atom; ++imo) {
          // Check how many electrons are left to add
          double _nelec = _occ_atom_a.sum();
          if(_nelec < 0.1) break;

          // Check to which shell the orbital belongs to
          int                 lang = -1;
          std::vector<double> normang_a(4, 0.0);
          for(size_t ishell = 0; ishell < obs.size(); ++ishell) {
            int l   = obs[ishell].contr[0].l;
            int bf1 = shell2bf[ishell];
            int bf2 = bf1 + obs[ishell].size() - 1;
            if(l > 3) continue;
            for(int ibf = bf1; ibf <= bf2; ++ibf) {
              normang_a[l] += C_a_atom(ibf, imo) * C_a_atom(ibf, imo);
            }
            if(normang_a[l] > 0.1) {
              lang = l;
              break;
            }
          }

          // If MO is from angular momentum > f skip it
          if(lang < 0) continue;

          // Skip if no more electrons to add to this shell
          if(_occ_atom_a(lang) < 0.1) continue;

          // Distribute electrons evenly in all degenerate orbitals
          double _nocc = std::min(_occ_atom_a(lang) / (2.0 * lang + 1.0), 1.0);
          for(int j = 0; j < 2 * lang + 1; ++j) {
            _occ_atom_a(lang) -= _nocc;
            // D_a_atom += _nocc * C_a_atom.col(imo + j) * C_a_atom.col(imo + j).transpose();
            occvec(imo + j) = _nocc;
          }
          imo += 2 * lang;
        }
        D_a_atom = C_a_atom * occvec.asDiagonal() * C_a_atom.transpose();

        // Beta
        occvec.setZero();
        for(size_t imo = 0; imo < nao_atom; ++imo) {
          // Check how many electrons are left to add
          double _nelec = _occ_atom_b.sum();
          if(_nelec < 0.1) break;

          // Check to which shell the orbital belongs to
          int                 lang = -1;
          std::vector<double> normang_b(4, 0.0);
          for(size_t ishell = 0; ishell < obs.size(); ++ishell) {
            int l   = obs[ishell].contr[0].l;
            int bf1 = shell2bf[ishell];
            int bf2 = bf1 + obs[ishell].size() - 1;
            if(l > 3) continue;
            for(int ibf = bf1; ibf <= bf2; ++ibf) {
              normang_b[l] += C_b_atom(ibf, imo) * C_b_atom(ibf, imo);
            }
            if(normang_b[l] > 0.1) {
              lang = l;
              break;
            }
          }

          // If shell is from angular momentum > f skip it
          if(lang < 0) continue;

          // Skip if no more electrons to add to this shell
          if(_occ_atom_b(lang) < 0.1) continue;

          // Distribute electrons evenly in all degenerate orbitals
          double _nocc = std::min(_occ_atom_b(lang) / (2 * lang + 1), 1.0);
          for(int j = 0; j < 2 * lang + 1; ++j) {
            _occ_atom_b(lang) -= _nocc;
            occvec(imo + j) = _nocc;
            // D_b_atom += _nocc * C_b_atom.col(imo + j) * C_b_atom.col(imo + j).transpose();
          }
          imo += 2 * lang;
        }
        D_b_atom = C_b_atom * occvec.asDiagonal() * C_b_atom.transpose();

        if(!spin_polarized) {
          D_a_atom = 0.5 * (D_a_atom + D_b_atom);
          D_b_atom = D_a_atom;
        }
      }

      auto D_a_diff = D_a_atom - D_a_atom_last;
      auto D_b_diff = D_b_atom - D_b_atom_last;
      D_a_atom -= 0.3 * D_a_diff;
      D_b_atom -= 0.3 * D_b_diff;
      rmsd_a_atom = std::max(D_a_diff.norm(), D_b_diff.norm());

      // if (rank==0) cout << std::setprecision(6) << rmsd_a_atom << endl;

      if(iter_atom > 200) break;

    } while(fabs(rmsd_a_atom) > 1e-5);

    if(rank == 0) {
      D_tot_a.block(indx, indx, nao_atom, nao_atom) = D_a_atom;
      D_tot_b.block(indx, indx, nao_atom, nao_atom) = D_b_atom;
    }

    atom_loc[es] = indx;
    indx += nao_atom;
    ++iatom;

    atom.pop_back();

    Tensor<TensorType>::deallocate(F1tmp1_a_atom2, F1tmp1_b_atom2, H_atom, S_atom);
  }

  // One-shot refinement
  if(rank == 0) {
    // D_tot_a -> etensors.D_alpha
    if(is_rhf) { etensors.D_alpha += D_tot_b; }

    if(is_uhf) etensors.D_beta = D_tot_b;

    D_tot_b.setZero();
  }

  if(rank == 0) {
    eigen_to_tamm_tensor(ttensors.D_alpha, etensors.D_alpha);
    if(is_uhf) { eigen_to_tamm_tensor(ttensors.D_beta, etensors.D_beta); }
  }
  ec.pg().barrier();

  if((scf_vars.do_dens_fit && !scf_vars.direct_df) && rank == 0) { etensors.G_alpha.resize(0, 0); }

  if(scf_vars.do_dens_fit &&
     !(scf_vars.direct_df || chem_env.sys_data.is_ks || chem_env.sys_data.do_snK)) {
    etensors.D_alpha.resize(0, 0);
    if(is_uhf) etensors.D_beta.resize(0, 0);
  }

  // needed only for 4c HF
  if(rank != 0 && (!scf_vars.do_dens_fit || scf_vars.direct_df || chem_env.sys_data.is_ks ||
                   chem_env.sys_data.do_snK)) {
    tamm_to_eigen_tensor(ttensors.D_alpha, etensors.D_alpha);
    if(is_uhf) { tamm_to_eigen_tensor(ttensors.D_beta, etensors.D_beta); }
  }

  ec.pg().barrier();

  auto ig2     = std::chrono::high_resolution_clock::now();
  auto ig_time = std::chrono::duration_cast<std::chrono::duration<double>>((ig2 - ig1)).count();
  if(ec.print())
    std::cout << std::fixed << std::setprecision(2) << "Time taken for SAD: " << ig_time << " secs"
              << endl;
}

template<typename T, int ndim>
void exachem::scf::SCFGuess::t2e_hf_helper(const ExecutionContext& ec, tamm::Tensor<T>& ttensor,
                                           Matrix& etensor, const std::string& ustr) {
  const string pstr = "(" + ustr + ")";

  const auto rank = ec.pg().rank();
  const auto N    = etensor.rows(); // TODO

  if(rank == 0) tamm_to_eigen_tensor(ttensor, etensor);
  ec.pg().barrier();
  std::vector<T> Hbufv(N * N);
  T*             Hbuf            = &Hbufv[0]; // Hbufv.data();
  Eigen::Map<Matrix>(Hbuf, N, N) = etensor;
  // GA_Brdcst(Hbuf,N*N*sizeof(T),0);
  ec.pg().broadcast(Hbuf, N * N, 0);
  etensor = Eigen::Map<Matrix>(Hbuf, N, N);
  Hbufv.clear();
  Hbufv.shrink_to_fit();
}

template void
exachem::scf::SCFGuess::compute_sad_guess<double>(ExecutionContext& ec, ChemEnv& chem_env,
                                                  SCFVars& scf_vars, ScalapackInfo& scalapack_info,
                                                  EigenTensors& etensors, TAMMTensors& ttensors);

template void exachem::scf::scf_guess::subshell_occvec<double>(double& occvec, size_t size,
                                                               size_t& ne);

template const std::vector<double>
exachem::scf::scf_guess::compute_ao_occupation_vector<double>(size_t Z);

template void exachem::scf::SCFGuess::compute_dipole_ints<double>(
  ExecutionContext& ec, const SCFVars& spvars, Tensor<TensorType>& tensorX,
  Tensor<TensorType>& tensorY, Tensor<TensorType>& tensorZ, std::vector<libint2::Atom>& atoms,
  libint2::BasisSet& shells, libint2::Operator otype);

template void exachem::scf::SCFGuess::compute_1body_ints<double>(
  ExecutionContext& ec, const SCFVars& scf_vars, Tensor<TensorType>& tensor1e,
  std::vector<libint2::Atom>& atoms, libint2::BasisSet& shells, libint2::Operator otype);

template void exachem::scf::SCFGuess::compute_pchg_ints<double>(
  ExecutionContext& ec, const SCFVars& scf_vars, Tensor<TensorType>& tensor1e,
  std::vector<std::pair<double, std::array<double, 3>>>& q, libint2::BasisSet& shells,
  libint2::Operator otype);

template void exachem::scf::SCFGuess::compute_ecp_ints(
  ExecutionContext& ec, const SCFVars& scf_vars, Tensor<TensorType>& tensor1e,
  std::vector<libecpint::GaussianShell>& shells, std::vector<libecpint::ECP>& ecps);

template void exachem::scf::SCFGuess::scf_diagonalize<double>(Scheduler& sch, ChemEnv& chem_env,
                                                              SCFVars&       scf_vars,
                                                              ScalapackInfo& scalapack_info,
                                                              TAMMTensors&   ttensors,
                                                              EigenTensors&  etensors);
template void exachem::scf::SCFGuess::t2e_hf_helper<double, 2>(const ExecutionContext& ec,
                                                               tamm::Tensor<double>&   ttensor,
                                                               Matrix& etensor, const std::string&);
