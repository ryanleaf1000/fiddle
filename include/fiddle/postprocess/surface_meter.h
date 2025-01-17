#ifndef included_fiddle_postprocess_surface_meter_h
#define included_fiddle_postprocess_surface_meter_h

#include <fiddle/base/config.h>

#include <fiddle/postprocess/meter_base.h>

#include <deal.II/base/point.h>
#include <deal.II/base/smartpointer.h>
#include <deal.II/base/tensor.h>

#include <deal.II/dofs/dof_handler.h>

#include <deal.II/fe/mapping.h>

#include <deal.II/lac/la_parallel_vector.h>

#include <tbox/Pointer.h>

#include <memory>
#include <utility>
#include <vector>

namespace SAMRAI
{
  namespace hier
  {
    template <int>
    class PatchHierarchy;
  }
} // namespace SAMRAI

namespace fdl
{
  template <int, int, int>
  class PointValues;
}

namespace fdl
{
  using namespace dealii;

  /**
   * Class for integrating Cartesian-grid values on codimension one surfaces
   * (colloquially a 'meter mesh').
   *
   * This class constructs a codimension one mesh in a dimension-dependent way:
   *
   * - in 3D, the provided points are treated as a closed loop surrounding some
   *   surface. Nearest points will be connected by line segments to form the
   *   boundary of a triangulation.
   * - in 2D, the provided points are treated as line segments - i.e., each
   *   adjacent pair of points define at least one element.
   *
   * This is because, in 2D, one may want to create a meter mesh corresponding
   * to a line rather than a closed loop.  To make a closed loop in 2D simply
   * make the first and last points equal.
   *
   * In both cases, the Triangulation created by this class will have elements
   * with side lengths approximately equal to the Cartesian grid cell length
   * (i.e., MFAC = 1).
   *
   * The velocity of the meter is the mean velocity of the boundary of the
   * meter - e.g., for channel flow, one can specify a mesh with points on the
   * top and bottom of the channel and then the meter velocity will equal the
   * wall velocity. This choice lets one compute fluxes through the meter
   * correctly (as the reference frame has a nonzero velocity). To get
   * absolute instead of relative fluxes simply set the input velocity values
   * to zero.
   *
   * @warning Due to the way IBAMR computes cell indices, points lying on the
   * upper boundaries of the computational domain may not have correct
   * interpolated values. If you want to compute values on the upper boundary
   * then you should adjust your points slightly using, e.g.,
   * std::nexttoward().
   */
  template <int dim, int spacedim = dim>
  class SurfaceMeter : public MeterBase<dim - 1, spacedim>
  {
  public:
    /**
     * Constructor.
     *
     * @param[in] mapping Mapping defined in reference coordinates (e.g., the
     * mapping returned by Part::get_mapping())
     *
     * @param[in] position_dof_handler DoFHandler describing the position and
     * velocity finite element spaces.
     *
     * @param[in] boundary_points Points, in reference configuration coordinates
     * (i.e., they are on the interior or boundary of the Triangulation),
     * describing the boundary of the meter mesh. These points typically outline
     * a disk and typically come from a node set defined on the Triangulation
     * associated
     * with @p dof_handler.
     *
     * @warning This function uses PointValues to compute the positions of the
     * nodes, which may, in parallel, give slightly different results (on the
     * level of machine precision) based on the cell partitioning. In unusual
     * cases this can cause Triangle to generate slightly different
     * triangulations - i.e., the exact meter Triangulation may depend on the
     * number of processors.
     */
    SurfaceMeter(const Mapping<dim, spacedim>       &mapping,
                 const DoFHandler<dim, spacedim>    &position_dof_handler,
                 const std::vector<Point<spacedim>> &boundary_points,
                 tbox::Pointer<hier::PatchHierarchy<spacedim>> patch_hierarchy,
                 const LinearAlgebra::distributed::Vector<double> &position,
                 const LinearAlgebra::distributed::Vector<double> &velocity);

    /**
     * Alternate constructor which copies a pre-existing surface Triangulation.
     */
    SurfaceMeter(const Triangulation<dim - 1, spacedim>       &tria,
                 tbox::Pointer<hier::PatchHierarchy<spacedim>> patch_hierarchy);

    /**
     * Alternate constructor which uses purely nodal data instead of finite
     * element fields.
     */
    SurfaceMeter(const std::vector<Point<spacedim>>           &boundary_points,
                 const std::vector<Tensor<1, spacedim>>       &velocity,
                 tbox::Pointer<hier::PatchHierarchy<spacedim>> patch_hierarchy);

    /**
     * Destructor. See the note in ~MeterBase().
     */
    virtual ~SurfaceMeter();

    /**
     * Whether or not the SurfaceMeter was set up with a codimension zero mesh.
     */
    bool
    uses_codim_zero_mesh() const;

    /**
     * Reinitialize the meter mesh to have its coordinates specified by @p
     * position and velocity by @p velocity.
     *
     * @note This function may only be called if the object was originally set
     * up with finite element data.
     */
    void
    reinit(const LinearAlgebra::distributed::Vector<double> &position,
           const LinearAlgebra::distributed::Vector<double> &velocity);

    /**
     * Alternative reinitialization function which (like the alternative
     * constructor) uses purely nodal data.
     */
    void
    reinit(const std::vector<Point<spacedim>>     &boundary_points,
           const std::vector<Tensor<1, spacedim>> &velocity);

    /**
     * Alternative reinitialization function which only updates the internal
     * data structures to account for the PatchHierarchy being regridded.
     *
     * @note This function is only implemented for the uses_codim_zero_mesh()
     * == false case and will throw an exception otherwise.
     */
    void
    reinit();

    /**
     * Return the mean velocity of the meter itself computed from the inputs
     * to the ctor or reinit() functions.
     *
     *
     * This value is computed in one of two ways:
     * - If the object is initialized from pointwise data, then the mean
     *   velocity is simply the average of the provided velocities.
     *
     * - If the object is initialized from FE field data, then in 2D this is
     *   the average of the pointwise velocities. In 3D it is the mean value
     *   of the velocity field computed on the boundary.
     */
    virtual Tensor<1, spacedim>
    get_mean_velocity() const;

    /**
     * Compute both the flux and the flux of some quantity through the meter
     * mesh and the mean normal vector of the mesh.
     *
     * @note The normal vector's sign depends on the orientation of the
     * Triangulation - see the deal.II glossary entry on "Direction flags" for
     * more information. This value is well-defined but might have the wrong
     * sign for your application. For example, if a meter is at a boundary and
     * you want to measure outflow, then you should check that the normal vector
     * points out of the domain.
     */
    virtual std::pair<double, Tensor<1, spacedim>>
    compute_flux(const int data_idx, const std::string &kernel_name) const;

    /**
     * Compute the mean normal vector. This is useful for checking the
     * orientation of the mesh.
     */
    virtual Tensor<1, spacedim>
    compute_mean_normal_vector() const;

  protected:
    /**
     * Reinitialize the stored Triangulation.
     *
     * If the points are located on a codimension zero mesh then
     * place_additional_boundary_vertices should be false. If they come from a
     * list of points then it should typically be true. In the first case we
     * want to avoid adding more boundary points since we will move vertices
     * to match the exact coordinates of vertices on the codimension zero
     * mesh. In the second, if we are in 2D then we typically want to compute
     * flow through a surface: the best way to do this is to specify two
     * points and then add more.
     */
    void
    reinit_tria(const std::vector<Point<spacedim>> &boundary_points,
                const bool place_additional_boundary_vertices);

    /**
     * Reinitialize the mean velocity of the meter itself from values of the
     * velocity specified at the boundary nodes. This function assumes that the
     * first [0, N - 1] nodes are on the boundary.
     *
     * @note In the sequence of reinitialization this should typically be called
     * last since it requires the Triangulation and FE data to first be set up.
     */
    void
    reinit_mean_velocity(
      const std::vector<Tensor<1, spacedim>> &velocity_values);

    /**
     * Internal reinitialization function which updates all data structures to
     * account for possible meter movement. Call the other protected reinit_*()
     * functions in the right order.
     */
    void
    internal_reinit(const bool                              reinit_tria,
                    const std::vector<Point<spacedim>>     &boundary_points,
                    const std::vector<Tensor<1, spacedim>> &velocity_values,
                    const bool place_additional_boundary_vertices);

    /**
     * Original Mapping.
     */
    SmartPointer<const Mapping<dim, spacedim>> mapping;

    /**
     * Original DoFHandler.
     */
    SmartPointer<const DoFHandler<dim, spacedim>> position_dof_handler;

    /**
     * PointValues object for computing the mesh's position.
     */
    std::unique_ptr<PointValues<spacedim, dim, spacedim>> point_values;

    /**
     * Mean meter velocity.
     */
    Tensor<1, spacedim> mean_velocity;
  };


  // --------------------------- inline functions --------------------------- //

  template <int dim, int spacedim>
  Tensor<1, spacedim>
  SurfaceMeter<dim, spacedim>::get_mean_velocity() const
  {
    return mean_velocity;
  }
} // namespace fdl

#endif
