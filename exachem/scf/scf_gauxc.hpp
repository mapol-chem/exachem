#if defined(USE_GAUXC)
#include <gauxc/molecular_weights.hpp>
#include <gauxc/molgrid/defaults.hpp>
#include <gauxc/xc_integrator.hpp>
#include <gauxc/xc_integrator/impl.hpp>
#include <gauxc/xc_integrator/integrator_factory.hpp>
#endif

#if defined(USE_GAUXC)
#include "common/chemenv.hpp"
#include "common/options/input_options.hpp"
#include "scf/scf_eigen_tensors.hpp"
#include "scf/scf_tamm_tensors.hpp"
#include "scf/scf_vars.hpp"

namespace igauxc_util {

std::tuple<std::shared_ptr<GauXC::XCIntegrator<Matrix>>, double>
setup_gauxc(ExecutionContext& ec, const ChemEnv& chem_env, const SCFVars& scf_vars);

GauXC::Molecule make_gauxc_molecule(const std::vector<libint2::Atom>& atoms);

GauXC::BasisSet<double> make_gauxc_basis(const libint2::BasisSet& basis);

template<typename TensorType>
TensorType compute_xcf(ExecutionContext& ec, ChemEnv& chem_env, TAMMTensors& ttensors,
                       EigenTensors& etensors, GauXC::XCIntegrator<Matrix>& xc_integrator);

} // namespace igauxc_util
#endif