#include <deal.II/base/function_lib.h>
#include <deal.II/base/index_set.h>
#include <deal.II/base/mpi.h>

#include <deal.II/distributed/shared_tria.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_q.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/tria.h>

#include <deal.II/lac/petsc_vector.h>
#include <deal.II/lac/vector.h>

#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/vector_tools.h>

#include <fiddle/overlap_partitioning_tools.h>
#include <fiddle/overlap_triangulation.h>
#include <fiddle/scatter.h>

#include <cmath>
#include <fstream>
#include <iostream>

using namespace dealii;

int
main(int argc, char **argv)
{
  Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);
  const auto                       mpi_comm = MPI_COMM_WORLD;
  const auto rank = Utilities::MPI::this_mpi_process(mpi_comm);
  parallel::shared::Triangulation<2> native_tria(mpi_comm);
  GridGenerator::hyper_ball(native_tria);
  native_tria.refine_global(1);
  for (unsigned int i = 0; i < 3; ++i)
    {
      for (const auto &cell : native_tria.active_cell_iterators())
        if (cell->barycenter()[0] > 0.0)
          cell->set_refine_flag();
      native_tria.execute_coarsening_and_refinement();
    }

  {
    GridOut       go;
    std::ofstream out("tria-1.eps");
    go.write_eps(native_tria, out);
  }

  BoundingBox<2> bbox;
  switch (rank)
    {
      case 0:
        bbox = BoundingBox<2>(
          std::make_pair(Point<2>(0.0, 0.0), Point<2>(1.0, 1.0)));
        break;
      case 1:
        bbox = BoundingBox<2>(
          std::make_pair(Point<2>(-1.0, 0.0), Point<2>(0.0, 1.0)));
        break;
      case 2:
        bbox = BoundingBox<2>(
          std::make_pair(Point<2>(-1.0, -1.0), Point<2>(0.0, 0.0)));
        break;
      case 3:
        bbox = BoundingBox<2>(
          std::make_pair(Point<2>(0.0, -1.0), Point<2>(1.0, 0.0)));
        break;
      default:
        Assert(false, ExcNotImplemented());
    }

  fdl::OverlapTriangulation<2> ib_tria(native_tria, {bbox});

  {
    GridOut       go;
    std::ofstream out("tria-2-" + std::to_string(rank) + ".eps");
    go.write_eps(ib_tria, out);
  }

  FE_Q<2>       fe(3);
  DoFHandler<2> native_dof_handler(native_tria);
  native_dof_handler.distribute_dofs(fe);
  DoFHandler<2> ib_dof_handler(ib_tria);
  ib_dof_handler.distribute_dofs(fe);

  IndexSet locally_relevant_dofs;
  DoFTools::extract_locally_relevant_dofs(native_dof_handler,
                                          locally_relevant_dofs);
#define USE_FDL_SCATTER 1
#if USE_FDL_SCATTER
  // TODO: VT::interpolate requires ghost data with LA::d::V, not sure why
  LinearAlgebra::distributed::Vector<double> native_solution(
    native_dof_handler.locally_owned_dofs(), locally_relevant_dofs, mpi_comm);
#else
  PETScWrappers::MPI::Vector native_solution(
    native_dof_handler.locally_owned_dofs(), mpi_comm);
#endif
  VectorTools::interpolate(native_dof_handler,
                           Functions::CosineFunction<2>(),
                           native_solution);

  Vector<double> ib_solution(ib_dof_handler.n_dofs());

  const std::vector<types::global_dof_index> overlap_to_native =
    fdl::compute_overlap_to_native_dof_translation(ib_tria,
                                                   ib_dof_handler,
                                                   native_dof_handler);
#if USE_FDL_SCATTER
  fdl::Scatter<double> scatter(overlap_to_native,
                               native_dof_handler.locally_owned_dofs(),
                               mpi_comm);
  scatter.global_to_overlap_start(native_solution, 0, ib_solution);
  scatter.global_to_overlap_finish(native_solution, ib_solution);
#else
  Vector<double> localized_native_solution(native_solution);
  for (types::global_dof_index dof = 0; dof < ib_dof_handler.n_dofs(); ++dof)
    {
      ib_solution[dof] = localized_native_solution[overlap_to_native[dof]];
    }
#endif

  {
#if USE_FDL_SCATTER
    LinearAlgebra::distributed::Vector<double> ghosted_native_solution(
      native_dof_handler.locally_owned_dofs(), locally_relevant_dofs, mpi_comm);
#else
    PETScWrappers::MPI::Vector ghosted_native_solution(
      native_dof_handler.locally_owned_dofs(), locally_relevant_dofs, mpi_comm);
#endif
    ghosted_native_solution = native_solution;
    ghosted_native_solution.update_ghost_values();

    DataOut<2> data_out;
    data_out.attach_dof_handler(native_dof_handler);
    data_out.add_data_vector(ghosted_native_solution, "solution");
    data_out.build_patches();

    data_out.write_vtu_with_pvtu_record("./", "solution", 0, mpi_comm);
  }

  {
    DataOut<2> data_out;
    data_out.attach_dof_handler(ib_dof_handler);
    data_out.add_data_vector(ib_solution, "solution");
    data_out.build_patches();

    std::ofstream out("ib-solution-" + std::to_string(rank) + ".vtu");
    data_out.write_vtu(out);
  }
}
