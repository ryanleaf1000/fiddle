#ifndef included_fiddle_mechanics_part_h
#define included_fiddle_mechanics_part_h

#include <fiddle/base/exceptions.h>

#include <deal.II/base/bounding_box.h>
#include <deal.II/base/function.h>

#include <deal.II/dofs/dof_handler.h>

#include <deal.II/grid/tria.h>

#include <fiddle/mechanics/force_contribution.h>
#include <fiddle/mechanics/mechanics_values.h>

#include <memory>
#include <vector>

namespace fdl
{
  using namespace dealii;

  /**
   * Class encapsulating a single structure - essentially a wrapper that stores
   * the current position and velocity and can also compute the interior force
   * density.
   *
   * @todo In the future we should add an API that allows users to merge in
   * their own constraints to the position, force, or displacement systems. This
   * class also needs to learn how to set up hanging node constraints. This
   * might not be trivial - if we constrain the position space then that implies
   * constraints on the velocity space. This might also raise adjointness
   * concerns.
   */
  template <int dim, int spacedim = dim>
  class Part
  {
  public:
    /**
     * Constructor.
     */
    Part(const Triangulation<dim, spacedim> &tria,
         const FiniteElement<dim, spacedim> &fe,
         std::vector<std::unique_ptr<ForceContribution<dim, spacedim>>>
                                   force_contributions = {},
         const Function<spacedim> &initial_position =
           Functions::IdentityFunction<spacedim>(),
         const Function<spacedim> &initial_velocity =
           Functions::ZeroFunction<spacedim>(spacedim));

    /**
     * Move constructor.
     */
    Part(Part<dim, spacedim> &&part) = default;

    /**
     * Get a constant reference to the Triangulation.
     */
    const Triangulation<dim, spacedim> &
    get_triangulation() const;

    /**
     * Get a copy of the communicator.
     */
    MPI_Comm
    get_communicator() const;

    /**
     * Get a constant reference to the DoFHandler used for the position,
     * velocity, and force.
     */
    const DoFHandler<dim, spacedim> &
    get_dof_handler() const;

    /**
     * Get the shared vector partitioner for the position, velocity, and force.
     * Useful if users want to set up their own vectors and re-use the parallel
     * data layout for these finite element spaces.
     */
    std::shared_ptr<const Utilities::MPI::Partitioner>
    get_partitioner() const;

    /**
     * Get the current position of the structure.
     */
    const LinearAlgebra::distributed::Vector<double> &
    get_position() const;

    /**
     * Set the current position by copying.
     */
    void
    set_position(const LinearAlgebra::distributed::Vector<double> &X);

    /**
     * Set the current position from a temporary.
     */
    void
    set_position(LinearAlgebra::distributed::Vector<double> &&X);

    /**
     * Get the current velocity of the structure.
     */
    const LinearAlgebra::distributed::Vector<double> &
    get_velocity() const;

    /**
     * Set the current velocity by copying.
     */
    void
    set_velocity(const LinearAlgebra::distributed::Vector<double> &X);

    /**
     * Set the current velocity from a temporary.
     */
    void
    set_velocity(LinearAlgebra::distributed::Vector<double> &&X);

  protected:
    /**
     * Triangulation of the part.
     */
    SmartPointer<const Triangulation<dim, spacedim>> tria;

    /**
     * Finite element for the position, velocity and force. Since velocity is
     * the time derivative of position we need to use the same FE for both
     * spaces. Similarly, to maintain adjointness between force spreading and
     * velocity interpolation, we need to use the same space for force and
     * velocity.
     */
    SmartPointer<const FiniteElement<dim, spacedim>> fe;

    /**
     * DoFHandler for the position, velocity, and force.
     *
     * @todo Implement a move constructor for this so we don't need a pointer.
     */
    std::unique_ptr<DoFHandler<dim, spacedim>> dof_handler;

    /**
     * Partitioner for the position, velocity, and force vectors.
     */
    std::shared_ptr<Utilities::MPI::Partitioner> partitioner;

    // Position.
    LinearAlgebra::distributed::Vector<double> position;

    // Velocity.
    LinearAlgebra::distributed::Vector<double> velocity;

    // All the functions that compute part of the force.
    std::vector<std::unique_ptr<ForceContribution<dim, spacedim>>>
      force_contributions;
  };

  // ----------------------------- inline functions ----------------------------

  template <int dim, int spacedim>
    const Triangulation<dim, spacedim> &
  Part<dim, spacedim>::get_triangulation() const
    {
      Assert(tria, ExcFDLInternalError());
      return *tria;
    }

  template <int dim, int spacedim>
    MPI_Comm
    Part<dim, spacedim>::get_communicator() const
    {
      Assert(tria, ExcFDLInternalError());
      return tria->get_communicator();
    }

  template <int dim, int spacedim>
    const DoFHandler<dim, spacedim> &
    Part<dim, spacedim>::get_dof_handler() const
    {
      return *dof_handler;
    }

  template <int dim, int spacedim>
    std::shared_ptr<const Utilities::MPI::Partitioner>
    Part<dim, spacedim>::get_partitioner() const
    {
      return partitioner;
    }

  // Functions for getting and setting state vectors

  template <int dim, int spacedim>
  const LinearAlgebra::distributed::Vector<double> &
  Part<dim, spacedim>::get_position() const
  {
    return position;
  }

  template <int dim, int spacedim>
  void
  Part<dim, spacedim>::set_position(
    const LinearAlgebra::distributed::Vector<double> &pos)
  {
    // TODO loosen this check slightly or implement Partitioner::operator==
    Assert(pos.get_partitioner() == partitioner,
           ExcMessage("The partitioners must be equal"));
    position = pos;
  }

  template <int dim, int spacedim>
  void
  Part<dim, spacedim>::set_position(
    LinearAlgebra::distributed::Vector<double> &&pos)
  {
    // TODO loosen this check slightly or implement Partitioner::operator==
    Assert(pos.get_partitioner() == partitioner,
           ExcMessage("The partitioners must be equal"));
    position.swap(pos);
  }

  template <int dim, int spacedim>
  const LinearAlgebra::distributed::Vector<double> &
  Part<dim, spacedim>::get_velocity() const
  {
    return velocity;
  }

  template <int dim, int spacedim>
  void
  Part<dim, spacedim>::set_velocity(
    const LinearAlgebra::distributed::Vector<double> &vel)
  {
    // TODO loosen this check slightly or implement Partitioner::operator==
    Assert(vel.get_partitioner() == partitioner,
           ExcMessage("The partitioners must be equal"));
    velocity = vel;
  }

  template <int dim, int spacedim>
  void
  Part<dim, spacedim>::set_velocity(
    LinearAlgebra::distributed::Vector<double> &&vel)
  {
    // TODO loosen this check slightly or implement Partitioner::operator==
    Assert(vel.get_partitioner() == partitioner,
           ExcMessage("The partitioners must be equal"));
    velocity.swap(vel);
  }
} // namespace fdl

#endif
