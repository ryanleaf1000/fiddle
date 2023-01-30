#include <fiddle/base/exceptions.h>

#include <fiddle/mechanics/force_contribution_lib.h>
#include <fiddle/mechanics/mechanics_utilities.h>

#include <deal.II/base/quadrature_lib.h>

#include <deal.II/distributed/shared_tria.h>

#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_simplex_p.h>
#include <deal.II/fe/fe_simplex_p_bubbles.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/mapping_cartesian.h>
#include <deal.II/fe/mapping_fe.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_tools.h>

#include <deal.II/lac/la_parallel_vector.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/solver_cg.h>

#include <deal.II/matrix_free/matrix_free.h>
#include <deal.II/matrix_free/operators.h>

#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/vector_tools_integrate_difference.h>
#include <deal.II/numerics/vector_tools_interpolate.h>
#include <deal.II/numerics/vector_tools_rhs.h>

#include <cmath>
#include <fstream>

#include "../tests.h"

// Print values for a few different stresses - we don't yet have MMS for these

using namespace dealii;
using namespace SAMRAI;

template <int dim>
class Position : public Function<dim>
{
public:
  Position()
    : Function<dim>(dim)
  {}

  virtual double
  value(const Point<dim> &p, const unsigned int component = 0) const override
  {
    AssertIndexRange(component, dim);
    if (component == 0)
      return p[0] + 0.5 * std::sin(p[0]) * std::cos(p[1]);
    return p[1] + 0.5 * std::cos(p[0]) * std::sin(p[1]);
  }
};

template <int dim, int spacedim = dim>
void
test(const Mapping<dim, spacedim> &                    mapping,
     const Quadrature<dim> &                           quadrature,
     const DoFHandler<dim, spacedim> &                 dof_handler,
     const fdl::ForceContribution<dim, spacedim> &     stress,
     const LinearAlgebra::distributed::Vector<double> &position,
     const LinearAlgebra::distributed::Vector<double> &velocity,
     std::ostream &                                    output)
{
  FEValues<dim> fe_values(mapping,
                          dof_handler.get_fe(),
                          quadrature,
                          stress.get_update_flags());

  fdl::MechanicsValues<dim> me_values(fe_values,
                                      position,
                                      velocity,
                                      stress.get_mechanics_update_flags());

  std::vector<Tensor<2, dim>> stresses(quadrature.size());
  for (const auto &cell : dof_handler.active_cell_iterators())
    {
      fe_values.reinit(cell);
      me_values.reinit(cell);

      auto view = make_array_view(stresses.begin(), stresses.end());
      stress.compute_stress(0.0, me_values, cell, view);

      output << "cell = " << cell << " material id = " << cell->material_id()
             << std::endl;
      for (const auto &stress : stresses)
        output << "  " << stress << std::endl;
    }
}

int
main(int argc, char **argv)
{
  constexpr int fe_degree     = 2;
  constexpr int n_refinements = 2;

  constexpr int dim      = 2;
  constexpr int spacedim = dim;

  std::ofstream output("output");

  // setup deal.II stuff:
  Triangulation<dim, spacedim> tria;
  GridGenerator::hyper_cube(tria);
  tria.refine_global(n_refinements);
  GridTools::distort_random(0.25, tria);
  tria.begin_active()->set_material_id(1);
  (++tria.begin_active())->set_material_id(1);
  (++(++tria.begin_active()))->set_material_id(42);
  output << "Number of cells = " << tria.n_active_cells() << std::endl;

  FESystem<dim, spacedim>       fe(FE_Q<dim, spacedim>(fe_degree), spacedim);
  const QGauss<dim>             quadrature(2);
  const MappingQ<dim, spacedim> mapping(1);

  DoFHandler<dim, spacedim> dof_handler(tria);
  dof_handler.distribute_dofs(fe);

  IndexSet locally_owned_dofs, locally_relevant_dofs;
  locally_owned_dofs = dof_handler.locally_owned_dofs();
  DoFTools::extract_locally_relevant_dofs(dof_handler, locally_relevant_dofs);
  auto partitioner =
    std::make_shared<Utilities::MPI::Partitioner>(locally_owned_dofs,
                                                  locally_relevant_dofs,
                                                  MPI_COMM_SELF);


  LinearAlgebra::distributed::Vector<double> position(partitioner),
    velocity(partitioner);
  VectorTools::interpolate(dof_handler, Position<spacedim>(), position);

  // and the test itself.
  {
    std::vector<types::material_id> materials;
    materials.push_back(1u);
    fdl::ModifiedNeoHookeanStress<dim, spacedim> s1(quadrature,
                                                    10.0,
                                                    materials);

    output << "ModifiedNeoHookeanStress" << std::endl;
    test(mapping, quadrature, dof_handler, s1, position, velocity, output);
  }

  {
    std::vector<types::material_id> materials;
    materials.push_back(1u);
    materials.push_back(42u);
    fdl::ModifiedMooneyRivlinStress<dim, spacedim> s1(quadrature,
                                                      10.0,
                                                      20.0,
                                                      materials);

    output << "ModifiedMooneyRivlinStress" << std::endl;
    test(mapping, quadrature, dof_handler, s1, position, velocity, output);
  }
}
