/*
 * ExaChem: Open Source Exascale Computational Chemistry Software.
 *
 * Copyright 2023-2024 Pacific Northwest National Laboratory, Battelle Memorial Institute.
 *
 * See LICENSE.txt for details
 */

#include "exachem/scf/scf_compute.hpp"

void exachem::scf::SCFCompute::compute_shellpair_list(const ExecutionContext&  ec,
                                                      const libint2::BasisSet& shells,
                                                      SCFVars&                 scf_vars) {
  auto rank = ec.pg().rank();

  // compute OBS non-negligible shell-pair list
  std::tie(scf_vars.obs_shellpair_list, scf_vars.obs_shellpair_data) = compute_shellpairs(shells);
  size_t nsp                                                         = 0;
  for(auto& sp: scf_vars.obs_shellpair_list) { nsp += sp.second.size(); }
  if(rank == 0)
    std::cout << "# of {all,non-negligible} shell-pairs = {"
              << shells.size() * (shells.size() + 1) / 2 << "," << nsp << "}" << endl;
}

void exachem::scf::SCFCompute::compute_trafo(const libint2::BasisSet& shells,
                                             EigenTensors&            etensors) {
  std::vector<Matrix>& CtoS = etensors.trafo_ctos;
  std::vector<Matrix>& StoC = etensors.trafo_stoc;
  int                  lmax = shells.max_l();

  static constexpr std::array<int64_t, 21> fac = {{1LL,
                                                   1LL,
                                                   2LL,
                                                   6LL,
                                                   24LL,
                                                   120LL,
                                                   720LL,
                                                   5040LL,
                                                   40320LL,
                                                   362880LL,
                                                   3628800LL,
                                                   39916800LL,
                                                   479001600LL,
                                                   6227020800LL,
                                                   87178291200LL,
                                                   1307674368000LL,
                                                   20922789888000LL,
                                                   355687428096000LL,
                                                   6402373705728000LL,
                                                   121645100408832000LL,
                                                   2432902008176640000LL}};

  auto binomial = [](int i, int j) -> int {
    return (j < 0 || j > i) ? 0 : fac[i] / (fac[j] * fac[i - j]);
  };

  for(int l = 0; l <= lmax; l++) {
    int    c_size = (l + 1) * (l + 2) / 2;
    int    s_size = 2 * l + 1;
    Matrix trafo  = Matrix::Zero(s_size, c_size);
    double norm2  = 1.0 / std::sqrt(((double) fac[2 * l]) / (pow(2.0, 2 * l) * fac[l]));

    for(int lx = l, ic = 0; lx >= 0; lx--) {
      for(int ly = l - lx; ly >= 0; ly--, ic++) {
        int    lz     = l - lx - ly;
        double norm1  = 1.0 / std::sqrt(((double) fac[2 * lx] * fac[2 * ly] * fac[2 * lz]) /
                                        (pow(2.0, 2 * l) * fac[lx] * fac[ly] * fac[lz]));
        double factor = norm1 / norm2;

        for(int m = -l, is = 0; m <= l; m++, is++) {
          int ma = abs(m);
          int j  = lx + ly - ma;
          if(j < 0 || j % 2 == 1) continue;
          j = j / 2;
          double s1{0.0};

          for(int i = 0; i <= (l - ma) / 2; i++) {
            double s2{0.0};
            for(int k = 0; k <= j; k++) {
              double s{0.0};
              int    expo;
              if((m < 0 && abs(ma - lx) % 2 == 1) || (m > 0 && abs(ma - lx) % 2 == 0)) {
                expo = (ma - lx + 2 * k) / 2;
                s    = pow(-1.0, expo) * std::sqrt(2.0);
              }
              else if(m == 0 && lx % 2 == 0) {
                expo = k - lx / 2;
                s    = pow(-1.0, expo);
              }
              else { s = 0.0; }
              s2 += binomial(j, k) * binomial(ma, lx - 2 * k) * s;
            }
            s1 += binomial(l, i) * binomial(i, j) * fac[2 * l - 2 * i] * pow(-1.0, i) * s2 /
                  fac[l - ma - 2 * i];
          }
          trafo(is, ic) =
            factor * s1 / (fac[l] * pow(2.0, l)) *
            std::sqrt(((double) fac[2 * lx] * fac[2 * ly] * fac[2 * lz] * fac[l] * fac[l - ma]) /
                      (fac[lx] * fac[ly] * fac[lz] * fac[2 * l] * fac[l + ma]));
        }
      }
    }
    CtoS.push_back(trafo);
  }

  // Compute backtransformation
  for(int l = 0; l <= lmax; l++) {
    int    c_size = (l + 1) * (l + 2) / 2;
    int    s_size = 2 * l + 1;
    Matrix trafo  = Matrix::Zero(s_size, c_size);
    double norm2  = 1.0 / std::sqrt(fac[2 * l] / (pow(2.0, 2 * l) * fac[l]));

    for(int lx1 = l, ic1 = 0; lx1 >= 0; lx1--) {
      for(int ly1 = l - lx1; ly1 >= 0; ly1--, ic1++) {
        int    lz1    = l - lx1 - ly1;
        double s1     = std::sqrt(((double) fac[lx1] * fac[ly1] * fac[lz1]) /
                                  (fac[2 * lx1] * fac[2 * ly1] * fac[2 * lz1]));
        double norm11 = s1 * pow(2, l);
        for(int lx2 = l, ic2 = 0; lx2 >= 0; lx2--) {
          for(int ly2 = l - lx2; ly2 >= 0; ly2--, ic2++) {
            int lz2 = l - lx2 - ly2;
            int lx  = lx1 + lx2;
            int ly  = ly1 + ly2;
            int lz  = lz1 + lz2;
            if(lx % 2 == 1 || ly % 2 == 1 || lz % 2 == 1) continue;
            double s2     = std::sqrt(((double) fac[lx2] * fac[ly2] * fac[lz2]) /
                                      (fac[2 * lx2] * fac[2 * ly2] * fac[2 * lz2]));
            double norm12 = s2 * pow(2, l);
            double s      = fac[lx] * fac[ly] * fac[lz] * s1 * s2 /
                       (fac[lx / 2] * fac[ly / 2] * fac[lz / 2]) * norm2 / norm11 * norm2 / norm12;
            for(int is = 0; is <= 2 * l; is++) trafo(is, ic1) += s * CtoS[l](is, ic2);
          }
        }
      }
    }
    StoC.push_back(trafo);
  }
}

template<typename TensorType>
void exachem::scf::SCFCompute::compute_sdens_to_cdens(const libint2::BasisSet& shells,
                                                      Matrix& Spherical, Matrix& Cartesian,
                                                      EigenTensors& etensors) {
  std::vector<Matrix>& CtoS     = etensors.trafo_ctos;
  auto                 shell2bf = shells.shell2bf();
  int                  nsh      = shells.size();
  // int                  Nspher{0};
  int Ncart{0};
  for(auto& shell: shells) {
    int l = shell.contr[0].l;
    // Nspher += 2 * l + 1;
    Ncart += ((l + 1) * (l + 2)) / 2;
  }

  Cartesian         = Matrix::Zero(Ncart, Ncart);
  int bf1_cartesian = 0;
  for(int sh1 = 0; sh1 < nsh; sh1++) {
    int l1            = shells[sh1].contr[0].l;
    int bf1_spherical = shell2bf[sh1];
    int n1_spherical  = shells[sh1].size();
    int n1_cartesian  = ((l1 + 1) * (l1 + 2)) / 2;
    int bf2_cartesian = 0;
    for(int sh2 = 0; sh2 < nsh; sh2++) {
      int l2            = shells[sh2].contr[0].l;
      int bf2_spherical = shell2bf[sh2];
      int n2_spherical  = shells[sh2].size();
      int n2_cartesian  = ((l2 + 1) * (l2 + 2)) / 2;
      for(int is1 = 0; is1 < n1_spherical; is1++) {
        for(int is2 = 0; is2 < n2_spherical; is2++) {
          for(int ic1 = 0; ic1 < n1_cartesian; ic1++) {
            for(int ic2 = 0; ic2 < n2_cartesian; ic2++) {
              Cartesian(bf1_cartesian + ic1, bf2_cartesian + ic2) +=
                CtoS[l1](is1, ic1) * Spherical(bf1_spherical + is1, bf2_spherical + is2) *
                CtoS[l2](is2, ic2);
            }
          }
        }
      }
      bf2_cartesian += n2_cartesian;
    }
    bf1_cartesian += n1_cartesian;
  }
}

template<typename TensorType>
void exachem::scf::SCFCompute::compute_cpot_to_spot(const libint2::BasisSet& shells,
                                                    Matrix& Spherical, Matrix& Cartesian,
                                                    EigenTensors& etensors) {
  std::vector<Matrix>& CtoS     = etensors.trafo_ctos;
  auto                 shell2bf = shells.shell2bf();
  int                  nsh      = shells.size();
  int                  Nspher{0};
  // int                  Ncart{0};
  for(auto& shell: shells) {
    int l = shell.contr[0].l;
    Nspher += 2 * l + 1;
    // Ncart += ((l + 1) * (l + 2)) / 2;
  }

  Spherical         = Matrix::Zero(Nspher, Nspher);
  int bf1_cartesian = 0;
  for(int sh1 = 0; sh1 < nsh; sh1++) {
    int l1            = shells[sh1].contr[0].l;
    int bf1_spherical = shell2bf[sh1];
    int n1_spherical  = shells[sh1].size();
    int n1_cartesian  = ((l1 + 1) * (l1 + 2)) / 2;
    int bf2_cartesian = 0;
    for(int sh2 = 0; sh2 < nsh; sh2++) {
      int l2            = shells[sh2].contr[0].l;
      int bf2_spherical = shell2bf[sh2];
      int n2_spherical  = shells[sh2].size();
      int n2_cartesian  = ((l2 + 1) * (l2 + 2)) / 2;
      for(int is1 = 0; is1 < n1_spherical; is1++) {
        for(int is2 = 0; is2 < n2_spherical; is2++) {
          for(int ic1 = 0; ic1 < n1_cartesian; ic1++) {
            for(int ic2 = 0; ic2 < n2_cartesian; ic2++) {
              Spherical(bf1_spherical + is1, bf2_spherical + is2) +=
                CtoS[l1](is1, ic1) * Cartesian(bf1_cartesian + ic1, bf2_cartesian + ic2) *
                CtoS[l2](is2, ic2);
            }
          }
        }
      }
      bf2_cartesian += n2_cartesian;
    }
    bf1_cartesian += n1_cartesian;
  }
}

std::tuple<int, double> exachem::scf::SCFCompute::compute_NRE(const ExecutionContext&     ec,
                                                              std::vector<libint2::Atom>& atoms) {
  // count the number of electrons
  auto nelectron = 0;
  for(size_t i = 0; i < atoms.size(); ++i) nelectron += atoms[i].atomic_number;

  // compute the nuclear repulsion energy
  double enuc = 0.0;
  for(size_t i = 0; i < atoms.size(); i++)
    for(size_t j = i + 1; j < atoms.size(); j++) {
      double xij = atoms[i].x - atoms[j].x;
      double yij = atoms[i].y - atoms[j].y;
      double zij = atoms[i].z - atoms[j].z;
      double r2  = xij * xij + yij * yij + zij * zij;
      double r   = sqrt(r2);
      enuc += atoms[i].atomic_number * atoms[j].atomic_number / r;
    }

  return std::make_tuple(nelectron, enuc);
}

void exachem::scf::SCFCompute::recompute_tilesize(ExecutionContext& ec, ChemEnv& chem_env,
                                                  bool is_df) {
  // heuristic to set tilesize to atleast 5% of nbf if user has not provided a tilesize
  const auto        N       = is_df ? chem_env.sys_data.ndf : chem_env.shells.nbf();
  const std::string jkey    = is_df ? "df_tilesize" : "tilesize";
  bool              user_ts = false;
  if(chem_env.jinput.contains("SCF"))
    user_ts = chem_env.jinput["SCF"].contains(jkey) ? true : false;
  tamm::Tile& tile_size = is_df ? chem_env.ioptions.scf_options.dfAO_tilesize
                                : chem_env.ioptions.scf_options.AO_tilesize;

  if(tile_size < N * 0.05 && !user_ts) {
    tile_size = std::ceil(N * 0.05);
    if(ec.print()) cout << "***** Reset tilesize to nbf*5% = " << tile_size << endl;
  }

  if(!is_df) chem_env.is_context.ao_tilesize = tile_size;
  else chem_env.is_context.dfao_tilesize = tile_size;
}

std::tuple<std::vector<size_t>, std::vector<Tile>, std::vector<Tile>>
exachem::scf::SCFCompute::compute_AO_tiles(const ExecutionContext& ec, ChemEnv& chem_env,
                                           libint2::BasisSet& shells, const bool is_df) {
  auto        rank        = ec.pg().rank();
  SCFOptions& scf_options = chem_env.ioptions.scf_options;

  int tile_size = scf_options.AO_tilesize;
  if(is_df) tile_size = scf_options.dfAO_tilesize;

  std::vector<Tile> AO_tiles;
  for(auto s: shells) AO_tiles.push_back(s.size());
  if(rank == 0) cout << "Number of AO tiles = " << AO_tiles.size() << endl;

  tamm::Tile          est_ts = 0;
  std::vector<Tile>   AO_opttiles;
  std::vector<size_t> shell_tile_map;
  for(size_t s = 0; s < shells.size(); s++) {
    est_ts += shells[s].size();
    if(est_ts >= (size_t) tile_size) {
      AO_opttiles.push_back(est_ts);
      shell_tile_map.push_back(s); // shell id specifying tile boundary
      est_ts = 0;
    }
  }
  if(est_ts > 0) {
    AO_opttiles.push_back(est_ts);
    shell_tile_map.push_back(shells.size() - 1);
  }

  return std::make_tuple(shell_tile_map, AO_tiles, AO_opttiles);
}

// returns {X,X^{-1},S_condition_number_after_conditioning}, where
// X is the generalized square-root-inverse such that X.transpose() * S * X = I
// columns of Xinv is the basis conditioned such that
// the condition number of its metric (Xinv.transpose . Xinv) <
// S_condition_number_threshold
void exachem::scf::SCFCompute::compute_orthogonalizer(ExecutionContext& ec, ChemEnv& chem_env,
                                                      SCFVars&       scf_vars,
                                                      ScalapackInfo& scalapack_info,
                                                      TAMMTensors&   ttensors) {
  auto        hf_t1       = std::chrono::high_resolution_clock::now();
  auto        rank        = ec.pg().rank();
  SCFOptions& scf_options = chem_env.ioptions.scf_options;

  // compute orthogonalizer X such that X.transpose() . S . X = I
  double XtX_condition_number;
  size_t obs_rank;
  double S_condition_number;
  double S_condition_number_threshold = scf_options.tol_lindep;

  std::tie(obs_rank, S_condition_number, XtX_condition_number) = gensqrtinv(
    ec, chem_env, scf_vars, scalapack_info, ttensors, false, S_condition_number_threshold);

  // TODO: Redeclare TAMM S1 with new dims?
  auto hf_t2   = std::chrono::high_resolution_clock::now();
  auto hf_time = std::chrono::duration_cast<std::chrono::duration<double>>((hf_t2 - hf_t1)).count();

  if(rank == 0)
    std::cout << std::fixed << std::setprecision(2)
              << "Time for computing orthogonalizer: " << hf_time << " secs" << endl
              << endl;
}

template<typename TensorType>
void exachem::scf::SCFCompute::compute_hamiltonian(ExecutionContext& ec, const SCFVars& scf_vars,
                                                   ChemEnv& chem_env, TAMMTensors& ttensors,
                                                   EigenTensors& etensors) {
  using libint2::Operator;
  // const size_t N = shells.nbf();
  auto rank = ec.pg().rank();

  std::vector<libint2::Atom>& atoms  = chem_env.atoms;
  libint2::BasisSet&          shells = chem_env.shells;

  ttensors.H1 = {scf_vars.tAO, scf_vars.tAO};
  ttensors.S1 = {scf_vars.tAO, scf_vars.tAO};
  ttensors.T1 = {scf_vars.tAO, scf_vars.tAO};
  ttensors.V1 = {scf_vars.tAO, scf_vars.tAO};
  Tensor<TensorType>::allocate(&ec, ttensors.H1, ttensors.S1, ttensors.T1, ttensors.V1);

  auto [mu, nu] = scf_vars.tAO.labels<2>("all");

  auto     hf_t1 = std::chrono::high_resolution_clock::now();
  SCFGuess scf_guess;
  scf_guess.compute_1body_ints(ec, scf_vars, ttensors.S1, atoms, shells, Operator::overlap);
  scf_guess.compute_1body_ints(ec, scf_vars, ttensors.T1, atoms, shells, Operator::kinetic);
  scf_guess.compute_1body_ints(ec, scf_vars, ttensors.V1, atoms, shells, Operator::nuclear);
  auto hf_t2   = std::chrono::high_resolution_clock::now();
  auto hf_time = std::chrono::duration_cast<std::chrono::duration<double>>((hf_t2 - hf_t1)).count();
  if(rank == 0)
    std::cout << std::fixed << std::setprecision(2) << std::endl
              << "Time for computing 1-e integrals T, V, S: " << hf_time << " secs" << endl;

  // Core Hamiltonian = T + V
  // clang-format off
  Scheduler{ec}
    (ttensors.H1(mu, nu)  =  ttensors.T1(mu, nu))
    (ttensors.H1(mu, nu) +=  ttensors.V1(mu, nu)).execute();
  // clang-format on
}

template<typename TensorType>
void exachem::scf::SCFCompute::compute_density(ExecutionContext& ec, ChemEnv& chem_env,
                                               const SCFVars& scf_vars,
                                               ScalapackInfo& scalapack_info, TAMMTensors& ttensors,
                                               EigenTensors& etensors) {
  auto do_t1 = std::chrono::high_resolution_clock::now();

  using T         = TensorType;
  Matrix& D_alpha = etensors.D_alpha;
  auto    rank    = ec.pg().rank();

  Scheduler sch{ec};

  SystemData& sys_data    = chem_env.sys_data;
  SCFOptions& scf_options = chem_env.ioptions.scf_options;
  const auto  is_uhf      = sys_data.is_unrestricted;

#if defined(USE_SCALAPACK)
  if(scalapack_info.pg.is_valid()) {
    Tensor<T> C_a   = from_block_cyclic_tensor(ttensors.C_alpha_BC);
    Tensor<T> C_o_a = tensor_block(C_a, {0, 0}, {sys_data.nbf_orig, sys_data.nelectrons_alpha});
    from_dense_tensor(C_o_a, ttensors.C_occ_a);
    Tensor<T>::deallocate(C_a, C_o_a);

    if(is_uhf) {
      Tensor<T> C_b   = from_block_cyclic_tensor(ttensors.C_beta_BC);
      Tensor<T> C_o_b = tensor_block(C_b, {0, 0}, {sys_data.nbf_orig, sys_data.nelectrons_beta});
      from_dense_tensor(C_o_b, ttensors.C_occ_b);
      Tensor<T>::deallocate(C_b, C_o_b);
    }
  }
#else
  Matrix& C_alpha = etensors.C_alpha;
  Matrix& C_beta  = etensors.C_beta;

  if(rank == 0) {
    // Fix Phase of the Orbitals
    Eigen::VectorXd max = etensors.C_alpha.colwise().maxCoeff();
    Eigen::VectorXd abs = etensors.C_alpha.cwiseAbs().colwise().maxCoeff();
    for(int imo = 0; imo < etensors.C_alpha.cols(); imo++) {
      if(max(imo) != abs(imo)) etensors.C_alpha.col(imo) *= -1.0;
    }

    if(is_uhf) {
      max = etensors.C_beta.colwise().maxCoeff();
      abs = etensors.C_beta.cwiseAbs().colwise().maxCoeff();
      for(int imo = 0; imo < etensors.C_beta.cols(); imo++) {
        if(max(imo) != abs(imo)) etensors.C_beta.col(imo) *= -1.0;
      }
    }

    Matrix& C_occ = etensors.C_occ;
    C_occ         = C_alpha.leftCols(sys_data.nelectrons_alpha);
    eigen_to_tamm_tensor(ttensors.C_occ_a, C_occ);
    if(is_uhf) {
      C_occ = C_beta.leftCols(sys_data.nelectrons_beta);
      eigen_to_tamm_tensor(ttensors.C_occ_b, C_occ);
    }
  }
  ec.pg().barrier();
#endif

  auto mu = scf_vars.mu, nu = scf_vars.nu;
  auto mu_oa = scf_vars.mu_oa, mu_ob = scf_vars.mu_ob;

  const T dfac = (is_uhf) ? 1.0 : 2.0;
  sch(ttensors.C_occ_aT(mu_oa, mu) = ttensors.C_occ_a(mu, mu_oa))(
    ttensors.D_alpha(mu, nu) = dfac * ttensors.C_occ_a(mu, mu_oa) * ttensors.C_occ_aT(mu_oa, nu));
  if(is_uhf) {
    sch(ttensors.C_occ_bT(mu_ob, mu) = ttensors.C_occ_b(mu, mu_ob))(
      ttensors.D_beta(mu, nu) = ttensors.C_occ_b(mu, mu_ob) * ttensors.C_occ_bT(mu_ob, nu));
  }
  sch.execute();

  // compute D in eigen for subsequent fock build
  if(!scf_vars.do_dens_fit || scf_vars.direct_df || chem_env.sys_data.is_ks ||
     chem_env.sys_data.do_snK) {
    tamm_to_eigen_tensor(ttensors.D_alpha, D_alpha);
    if(is_uhf) tamm_to_eigen_tensor(ttensors.D_beta, etensors.D_beta);
  }

  ec.pg().barrier();

  auto do_t2   = std::chrono::high_resolution_clock::now();
  auto do_time = std::chrono::duration_cast<std::chrono::duration<double>>((do_t2 - do_t1)).count();

  if(rank == 0 && scf_options.debug)
    std::cout << std::fixed << std::setprecision(2) << "density: " << do_time << "s " << std::endl;
}

std::tuple<shellpair_list_t, shellpair_data_t> exachem::scf::SCFCompute::compute_shellpairs(
  const libint2::BasisSet& bs1, const libint2::BasisSet& _bs2, const double threshold) {
  using libint2::BasisSet;
  using libint2::BraKet;
  using libint2::Engine;
  using libint2::Operator;

  const BasisSet& bs2           = (_bs2.empty() ? bs1 : _bs2);
  const auto      nsh1          = bs1.size();
  const auto      nsh2          = bs2.size();
  const auto      bs1_equiv_bs2 = (&bs1 == &bs2);

  // construct the 2-electron repulsion integrals engine
  Engine engine(Operator::overlap, std::max(bs1.max_nprim(), bs2.max_nprim()),
                std::max(bs1.max_l(), bs2.max_l()), 0);

  shellpair_list_t splist;

  const auto& buf = engine.results();

  // loop over permutationally-unique set of shells
  for(size_t s1 = 0l, s12 = 0l; s1 != nsh1; ++s1) {
    // mx.lock();
    if(splist.find(s1) == splist.end()) splist.insert(std::make_pair(s1, std::vector<size_t>()));
    // mx.unlock();

    auto n1 = bs1[s1].size(); // number of basis functions in this shell

    auto s2_max = bs1_equiv_bs2 ? s1 : nsh2 - 1;
    for(decltype(s2_max) s2 = 0; s2 <= s2_max; ++s2, ++s12) {
      // if (s12 % nthreads != thread_id) continue;

      auto on_same_center = (bs1[s1].O == bs2[s2].O);
      bool significant    = on_same_center;
      if(not on_same_center) {
        auto n2 = bs2[s2].size();
        engine.compute(bs1[s1], bs2[s2]);
        Eigen::Map<const Matrix> buf_mat(buf[0], n1, n2);
        auto                     norm = buf_mat.norm();
        significant                   = (norm >= threshold);
      }

      if(significant) {
        // mx.lock();
        splist[s1].emplace_back(s2);
        // mx.unlock();
      }
    }
  }

  // resort shell list in increasing order, i.e. splist[s][s1] < splist[s][s2] if s1 < s2
  // N.B. only parallelized over 1 shell index
  for(size_t s1 = 0l; s1 != nsh1; ++s1) {
    // if (s1 % nthreads == thread_id) {
    auto& list = splist[s1];
    std::sort(list.begin(), list.end());
  }
  // }

  // compute shellpair data assuming that we are computing to default_epsilon
  // N.B. only parallelized over 1 shell index
  const auto       max_engine_precision    = std::numeric_limits<double>::epsilon() / 1e10;
  const auto       ln_max_engine_precision = std::log(max_engine_precision);
  shellpair_data_t spdata(splist.size());

  for(size_t s1 = 0l; s1 != nsh1; ++s1) {
    // if (s1 % nthreads == thread_id) {
    for(const auto& s2: splist[s1]) {
      spdata[s1].emplace_back(
        std::make_shared<libint2::ShellPair>(bs1[s1], bs2[s2], ln_max_engine_precision));
    }
    // }
  }

  return std::make_tuple(splist, spdata);
} // END of compute_shellpairs()

template<libint2::Operator Kernel>
Matrix exachem::scf::SCFCompute::compute_schwarz_ints(
  ExecutionContext& ec, const SCFVars& scf_vars, const libint2::BasisSet& bs1,
  const libint2::BasisSet& _bs2, bool use_2norm,
  typename libint2::operator_traits<Kernel>::oper_params_type params) {
  using libint2::BasisSet;
  using libint2::BraKet;
  using libint2::Engine;

  // auto hf_t1 = std::chrono::high_resolution_clock::now();

  const BasisSet& bs2           = (_bs2.empty() ? bs1 : _bs2);
  const auto      nsh1          = bs1.size();
  const auto      nsh2          = bs2.size();
  const auto      bs1_equiv_bs2 = (&bs1 == &bs2);

  EXPECTS(nsh1 == nsh2);
  Matrix K = Matrix::Zero(nsh1, nsh2);

  // construct the 2-electron repulsion integrals engine
  // !!! very important: cannot screen primitives in Schwarz computation !!!
  double epsilon = 0.0;
  Engine engine  = Engine(Kernel, std::max(bs1.max_nprim(), bs2.max_nprim()),
                          std::max(bs1.max_l(), bs2.max_l()), 0, epsilon, params);

  auto& buf = engine.results();

  const std::vector<size_t>& shell_tile_map = scf_vars.shell_tile_map;

  TiledIndexSpace    tnsh{IndexSpace{range(0, nsh1)}, static_cast<Tile>(std::ceil(nsh1 * 0.05))};
  Tensor<TensorType> schwarz{scf_vars.tAO, scf_vars.tAO};
  Tensor<TensorType> schwarz_mat{tnsh, tnsh};
  Tensor<TensorType>::allocate(&ec, schwarz_mat);
  Scheduler sch{ec};
  sch(schwarz_mat() = 0).execute();

  auto compute_schwarz_matrix = [&](const IndexVector& blockid) {
    auto bi0 = blockid[0];
    auto bi1 = blockid[1];

    // loop over permutationally-unique set of shells
    auto                  s1range_end   = shell_tile_map[bi0];
    decltype(s1range_end) s1range_start = 0l;
    if(bi0 > 0) s1range_start = shell_tile_map[bi0 - 1] + 1;

    for(auto s1 = s1range_start; s1 <= s1range_end; ++s1) {
      auto n1 = bs1[s1].size();

      auto                  s2range_end   = shell_tile_map[bi1];
      decltype(s2range_end) s2range_start = 0l;
      if(bi1 > 0) s2range_start = shell_tile_map[bi1 - 1] + 1;

      for(auto s2 = s2range_start; s2 <= s2range_end; ++s2) {
        auto n2  = bs2[s2].size();
        auto n12 = n1 * n2;

        // compute shell pair; return is the pointer to the buffer
        engine.compute2<Kernel, BraKet::xx_xx, 0>(bs1[s1], bs2[s2], bs1[s1], bs2[s2]);

        EXPECTS(buf[0] != nullptr && "turn off primitive screening to compute Schwarz ints");

        // to apply Schwarz inequality to individual integrals must use the diagonal elements
        // to apply it to sets of functions (e.g. shells) use the whole shell-set of ints here
        Eigen::Map<const Matrix> buf_mat(buf[0], n12, n12);
        auto norm2 = use_2norm ? buf_mat.norm() : buf_mat.lpNorm<Eigen::Infinity>();
        K(s1, s2)  = std::sqrt(norm2);
        if(bs1_equiv_bs2) K(s2, s1) = K(s1, s2);
      }
    }
  };

  block_for(ec, schwarz(), compute_schwarz_matrix);
  ec.pg().barrier();

  eigen_to_tamm_tensor_acc(schwarz_mat, K);
  ec.pg().barrier();
  K.resize(0, 0);

  K = tamm_to_eigen_matrix<TensorType>(schwarz_mat);
  Tensor<TensorType>::deallocate(schwarz_mat);

  return K;
} // END of compute_schwarz_ints()

template Matrix exachem::scf::SCFCompute::compute_schwarz_ints<libint2::Operator::coulomb>(
  ExecutionContext& ec, const SCFVars& scf_vars, const libint2::BasisSet& bs1,
  const libint2::BasisSet& bs2, bool use_2norm,
  typename libint2::operator_traits<libint2::Operator::coulomb>::oper_params_type params);

template void exachem::scf::SCFCompute::compute_sdens_to_cdens<double>(
  const libint2::BasisSet& shells, Matrix& Spherical, Matrix& Cartesian, EigenTensors& etensors);

template void exachem::scf::SCFCompute::compute_cpot_to_spot<double>(
  const libint2::BasisSet& shells, Matrix& Spherical, Matrix& Cartesian, EigenTensors& etensors);

template void exachem::scf::SCFCompute::compute_hamiltonian<double>(ExecutionContext& ec,
                                                                    const SCFVars&    scf_vars,
                                                                    ChemEnv&          chem_env,
                                                                    TAMMTensors&      ttensors,
                                                                    EigenTensors&     etensors);

template void exachem::scf::SCFCompute::compute_density<double>(
  ExecutionContext& ec, ChemEnv& chem_env, const SCFVars& scf_vars, ScalapackInfo& scalapack_info,
  TAMMTensors& ttensors, EigenTensors& etensors);
