#ifndef included_fiddle_interaction_interaction_utilities_h
#define included_fiddle_interaction_interaction_utilities_h

#include <fiddle/base/quadrature_family.h>

#include <fiddle/grid/overlap_tria.h>
#include <fiddle/grid/patch_map.h>

#include <fiddle/transfer/scatter.h>

#include <deal.II/base/bounding_box.h>
#include <deal.II/base/mpi_noncontiguous_partitioner.h>
#include <deal.II/base/quadrature.h>

#include <deal.II/distributed/shared_tria.h>

#include <deal.II/dofs/dof_handler.h>

#include <deal.II/fe/mapping.h>

#include <deal.II/lac/vector.h>

#include <ibtk/SAMRAIDataCache.h>
#include <ibtk/SAMRAIGhostDataAccumulator.h>

#include <PatchLevel.h>

#include <memory>
#include <vector>

// This file contains the functions that do all the actual interaction work -
// these are typically called by InteractionBase and its descendants and not
// directly by user code.

namespace fdl
{
  using namespace dealii;
  using namespace SAMRAI;

  /**
   * Tag cells in the patch hierarchy that intersect the provided bounding
   * boxes.
   */
  template <int spacedim, typename Number>
  void
  tag_cells(const std::vector<BoundingBox<spacedim, Number>> &bboxes,
            const int                                         tag_index,
            tbox::Pointer<hier::PatchLevel<spacedim>>         patch_level);

  /**
   * Compute the right-hand side used to project the velocity from Eulerian to
   * Lagrangian representation.
   *
   * @param[in] f_data_idx the SAMRAI patch data index we are interpolating. The
   * depth of the variable must match the number of components of the finite
   * element.
   *
   * @param[in] patch_map The mapping between SAMRAI patches and deal.II cells
   * which we will use for interpolation.
   *
   * @param[in] X_mapping Mapping from the reference configuration to the
   * current configuration of the mesh.
   *
   * @param[in] quadrature_indices This vector is indexed by the active cell
   * index - the value is the index into @p quadratures corresponding to the
   * correct quadrature rule on that cell.
   *
   * @param[in] quadratures The vector of quadratures we use to interpolate.
   *
   * @param[in] F_dof_handler DoFHandler for the finite element we are
   * interpolating onto.
   *
   * @param[in] F_mapping Mapping for computing values of the finite element
   * field on the reference configuration.
   *
   * @param[out] F_rhs The load vector populated by this operation.
   *
   * @note In general, an OverlappingTriangulation has no knowledge of whether
   * or not DoFs on its boundaries should be constrained. Hence information must
   * first be communicated between processes and then constraints should be
   * applied.
   */
  template <int dim, int spacedim = dim>
  void
  compute_projection_rhs(const int                           f_data_idx,
                         const PatchMap<dim, spacedim> &     patch_map,
                         const Mapping<dim, spacedim> &      X_mapping,
                         const std::vector<unsigned char> &  quadrature_indices,
                         const std::vector<Quadrature<dim>> &quadratures,
                         const DoFHandler<dim, spacedim> &   F_dof_handler,
                         const Mapping<dim, spacedim> &      F_mapping,
                         Vector<double> &                    F_rhs);

  /**
   * Compute (by adding into the patch index @p f_data_idx) the forces on the
   * Eulerian grid corresponding to the Lagrangian field F.
   *
   * @param[in] f_data_idx the SAMRAI patch data index into which we are
   * spreading. The depth of the variable must match the number of components of
   * the finite element.
   *
   * @param[inout] patch_map The mapping between SAMRAI patches and deal.II
   * cells. Though we do not modify this object directly, it is logically
   * non-const because we will modify the patches owned by the patch hierarchy
   * to which this object stores pointers.
   *
   * @param[in] X_mapping Mapping from the reference configuration to the
   * current configuration of the mesh.
   *
   * @param[in] quadrature_indices This vector is indexed by the active cell
   * index - the value is the index into @p quadratures corresponding to the
   * correct quadrature rule on that cell.
   *
   * @param[in] quadratures The vector of quadratures we use to interpolate.
   *
   * @param[in] F_dof_handler DoFHandler for the finite element we are
   * spreading from.
   *
   * @param[in] F_mapping Mapping for computing values of the finite element
   * field on the reference configuration.
   *
   * @param[in] F The finite element field we are spreading from.
   */
  template <int dim, int spacedim>
  void
  compute_spread(const int                           f_data_idx,
                 PatchMap<dim, spacedim> &           patch_map,
                 const Mapping<dim, spacedim> &      X_mapping,
                 const std::vector<unsigned char> &  quadrature_indices,
                 const std::vector<Quadrature<dim>> &quadratures,
                 const DoFHandler<dim, spacedim> &   F_dof_handler,
                 const Mapping<dim, spacedim> &      F_mapping,
                 const Vector<double> &              F);

} // namespace fdl
#endif
