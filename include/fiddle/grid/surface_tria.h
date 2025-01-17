#ifndef included_fiddle_surface_tria_h
#define included_fiddle_surface_tria_h

#include <fiddle/base/config.h>

#include <deal.II/base/point.h>

#include <deal.II/grid/tria.h>

#include <limits>
#include <vector>

namespace fdl
{
  using namespace dealii;

  namespace Triangle
  {
    /**
     * Parameters for the call to Triangle.
     */
    struct AdditionalData
    {
      AdditionalData()
        : min_angle(30.0)
        , target_element_area(std::numeric_limits<double>::max())
        , place_additional_boundary_vertices(false)
        , apply_fixup_routines(false)
      {}

      /**
       * Minimum angle in degrees. Large angles (e.g., 40) can cause the mesh
       * generator to create an unnecessarily large number of elements, so the
       * default value (or a lower one) is recommended.
       */
      double min_angle;

      /**
       * Target area. Defaults to elements with an edge length equal to the
       * distance between the first two nodes.
       */
      double target_element_area;

      /**
       * Whether or not additional vertices on the boundary (called Steiner
       * points) should be placed. Defaults to false.
       */
      bool place_additional_boundary_vertices;

      /**
       * Whether or not the resulting mesh should be postprocessed by deleting
       * duplicate or unused vertices. This defaults to false so that the
       * vertex ordering is the same between the created triangulation and the
       * input vertices.
       */
      bool apply_fixup_routines;
    };
  } // namespace Triangle

  /**
   * Triangulate a surface described by a set of vertices.
   *
   * To create a non-convex mesh this algorithm must determine edges from @p
   * vertices. Edges are detected in a three-step process:
   *
   * 1. Given a previous edge e0, the next adjacent edge e1 is chosen such
   *    that the new vertex is the closest vertex such that the angle between
   *    e0 and e1 is less than 180 degrees (i.e., no backtracking) and the new
   *    vertex is not part of any current edge.
   * 2. Should that search fail to find a new edge the 180 degree condition is
   *    removed.
   * 3. Should that search fail to find a new edge the new vertex condition is
   *    also removed.
   *
   * This algorithm works well with nearly-convex sets but may fail with
   * star-shaped domains with insufficiently many vertices.
   */
  void
  triangulate_segments(const std::vector<Point<2>>   &vertices,
                       Triangulation<2>              &tria,
                       const Triangle::AdditionalData additional_data = {});

  /**
   * Set up a planar mesh which best fits (in the least-squares sense) the three
   * dimensional points.
   *
   * As the output mesh is planar, this algorithm first projects all points onto
   * a plane.
   */
  Tensor<1, 3>
  create_planar_triangulation(
    const std::vector<Point<3>>   &points,
    Triangulation<2, 3>           &tria,
    const Triangle::AdditionalData additional_data = {});

  /**
   * Fit the Triangulation to a new set of boundary vertices. Inner vertices
   * are displaced according to the minimal surface (i.e, Laplace) equation.
   *
   * Only implemented for sequential Triangulations.
   */
  template <int dim, int spacedim = dim>
  void
  fit_boundary_vertices(const std::vector<Point<spacedim>> &new_vertices,
                        Triangulation<dim, spacedim>       &tria);

} // namespace fdl

#endif
