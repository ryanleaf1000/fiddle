#include <deal.II/base/index_set.h>
#include <deal.II/base/types.h>

#include <deal.II/dofs/dof_handler.h>

#include <fiddle/grid/overlap_tria.h>
#include <fiddle/transfer/overlap_partitioning_tools.h>

namespace fdl
{
  using namespace dealii;

  template <int dim, int spacedim = dim>
  std::vector<types::global_dof_index>
  compute_overlap_to_native_dof_translation(
    const fdl::OverlapTriangulation<dim, spacedim> &overlap_tria,
    const DoFHandler<dim, spacedim> &               overlap_dof_handler,
    const DoFHandler<dim, spacedim> &               native_dof_handler)
  {
    const MPI_Comm mpi_comm =
      overlap_tria.get_native_triangulation().get_communicator();
    Assert(&overlap_dof_handler.get_triangulation() == &overlap_tria,
           ExcMessage("The overlap DoFHandler should use the overlap tria"));
    Assert(&native_dof_handler.get_triangulation() ==
             &overlap_tria.get_native_triangulation(),
           ExcMessage("The native DoFHandler should use the native tria"));
    // Outline of the algorithm:
    //
    // 1. Determine which active cell indices the overlap tria needs.
    //
    // 2. Send (sorted) active cell indices. Use some_to_some for
    //    convenience.
    //
    // We now know who wants which dofs.
    //
    // 3. Pack the requested DoFs and send them back (use some_to_some
    //    again).
    //
    // 4. Loop over active cells to create the mapping between overlap dofs
    //    (purely local) and native dofs (distributed).

    // 1: determine required active cell indices:
    std::map<types::subdomain_id, std::vector<unsigned int>>
      native_active_cell_ids_on_overlap;
    for (const auto &cell : overlap_tria.active_cell_iterators())
      {
        if (cell->is_locally_owned())
          {
            const auto &native_cell  = overlap_tria.get_native_cell(cell);
            const auto  subdomain_id = native_cell->subdomain_id();
            native_active_cell_ids_on_overlap[subdomain_id].push_back(
              native_cell->active_cell_index());
          }
      }
    for (auto &pair : native_active_cell_ids_on_overlap)
      std::sort(pair.second.begin(), pair.second.end());

    // 2: send requested active cell indices:
    const std::map<types::subdomain_id, std::vector<unsigned int>>
      requested_native_active_cell_indices =
        Utilities::MPI::some_to_some(mpi_comm,
                                     native_active_cell_ids_on_overlap);

    // 3: pack dofs:
    std::map<types::subdomain_id, std::vector<types::global_dof_index>>
                dofs_on_native;
    const auto &fe = native_dof_handler.get_fe();
    Assert(fe.get_name() == overlap_dof_handler.get_fe().get_name(),
           ExcMessage("dof handlers should use the same FiniteElement"));
    // TODO: we could make this more efficient by sorting DH iterators by
    // active cell index and then getting them with lower_bound.
    for (const auto &pair : requested_native_active_cell_indices)
      {
        const types::subdomain_id        requested_rank      = pair.first;
        const std::vector<unsigned int> &active_cell_indices = pair.second;
        auto active_cell_index_ptr = active_cell_indices.cbegin();
        std::vector<types::global_dof_index> cell_dofs(fe.dofs_per_cell);
        // Note that we iterate over active cells in order
        for (const auto &cell : native_dof_handler.active_cell_iterators())
          {
            if (cell->active_cell_index() != *active_cell_index_ptr)
              continue;

            Assert(active_cell_index_ptr < active_cell_indices.end(),
                   ExcInternalError());
            dofs_on_native[requested_rank].push_back(*active_cell_index_ptr);
            cell->get_dof_indices(cell_dofs);
            dofs_on_native[requested_rank].push_back(cell_dofs.size());
            for (const auto dof : cell_dofs)
              dofs_on_native[requested_rank].push_back(dof);

            dofs_on_native[requested_rank].push_back(
              numbers::invalid_dof_index);
            ++active_cell_index_ptr;
            // we may run out of requested cells before we run out of owned
            // cells
            if (active_cell_index_ptr == active_cell_indices.end())
              break;
          }
      }

    const std::map<types::subdomain_id, std::vector<types::global_dof_index>>
      native_dof_indices =
        Utilities::MPI::some_to_some(mpi_comm, dofs_on_native);

    // we now have the native dofs on each cell in a packed format: active cell
    // index, dofs, sentinel. Make it easier to look up local cells by sorting
    // by global active cell indices:
    std::vector<typename DoFHandler<dim, spacedim>::active_cell_iterator>
      overlap_dh_cells;
    for (const auto &cell : overlap_dof_handler.active_cell_iterators())
      {
        if (cell->is_locally_owned())
          overlap_dh_cells.push_back(cell);
      }
    std::sort(overlap_dh_cells.begin(),
              overlap_dh_cells.end(),
              [&](const auto &a, const auto &b) {
                return overlap_tria.get_native_cell(a)->active_cell_index() <
                       overlap_tria.get_native_cell(b)->active_cell_index();
              });

    // 4:
    std::vector<std::pair<types::global_dof_index, types::global_dof_index>>
      overlap_to_native;
    for (const auto &pair : native_dof_indices)
      {
        const std::vector<types::global_dof_index> &native_dofs = pair.second;
        auto                                 packed_ptr = native_dofs.cbegin();
        std::vector<types::global_dof_index> native_cell_dofs(fe.dofs_per_cell);
        std::vector<types::global_dof_index> overlap_cell_dofs(
          fe.dofs_per_cell);

        while (packed_ptr < native_dofs.cend())
          {
            const auto active_cell_index = *packed_ptr;
            ++packed_ptr;
            // Find the overlap cell corresponding to the given active cell
            // index.
            const auto overlap_dh_cell = std::lower_bound(
              overlap_dh_cells.begin(),
              overlap_dh_cells.end(),
              active_cell_index,
              [&](const auto &a, const auto &val) {
                return overlap_tria.get_native_cell(a)->active_cell_index() <
                       val;
              });
            Assert(overlap_dh_cell != overlap_dh_cells.end(),
                   ExcInternalError());
            Assert(overlap_tria.get_native_cell(*overlap_dh_cell)
                       ->active_cell_index() == active_cell_index,
                   ExcInternalError());
            const auto n_dofs = *packed_ptr;
            ++packed_ptr;

            native_cell_dofs.clear();
            for (unsigned int i = 0; i < n_dofs; ++i)
              {
                native_cell_dofs.push_back(*packed_ptr);
                ++packed_ptr;
              }
            Assert(*packed_ptr == numbers::invalid_dof_index,
                   ExcInternalError());
            ++packed_ptr;

            // Copy data between orderings.
            (*overlap_dh_cell)->get_dof_indices(overlap_cell_dofs);
            for (unsigned int i = 0; i < n_dofs; ++i)
              {
                overlap_to_native.emplace_back(overlap_cell_dofs[i],
                                               native_cell_dofs[i]);
              }
          }
      }
    std::sort(overlap_to_native.begin(),
              overlap_to_native.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    overlap_to_native.erase(std::unique(overlap_to_native.begin(),
                                        overlap_to_native.end()),
                            overlap_to_native.end());
    std::vector<types::global_dof_index> native_indices(
      overlap_to_native.size());
    std::transform(overlap_to_native.begin(),
                   overlap_to_native.end(),
                   native_indices.begin(),
                   [](const auto &a) { return a.second; });

    // We finally have the contiguous array native_indices that gives us the
    // native dof for each overlap dof.
    return native_indices;
  }

  template std::vector<types::global_dof_index>
  compute_overlap_to_native_dof_translation(
    const fdl::OverlapTriangulation<2, 2> &overlap_tria,
    const DoFHandler<2, 2> &               overlap_dof_handler,
    const DoFHandler<2, 2> &               native_dof_handler);

  template std::vector<types::global_dof_index>
  compute_overlap_to_native_dof_translation(
    const fdl::OverlapTriangulation<2, 3> &overlap_tria,
    const DoFHandler<2, 3> &               overlap_dof_handler,
    const DoFHandler<2, 3> &               native_dof_handler);

  template std::vector<types::global_dof_index>
  compute_overlap_to_native_dof_translation(
    const fdl::OverlapTriangulation<3, 3> &overlap_tria,
    const DoFHandler<3, 3> &               overlap_dof_handler,
    const DoFHandler<3, 3> &               native_dof_handler);
} // namespace fdl