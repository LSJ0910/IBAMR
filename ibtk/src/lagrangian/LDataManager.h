// Filename: LDataManager.h
// Created on 01 Mar 2004 by Boyce Griffith
//
// Copyright (c) 2002-2010, Boyce Griffith
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of New York University nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef included_LDataManager
#define included_LDataManager

/////////////////////////////// INCLUDES /////////////////////////////////////

// C++ STDLIB INCLUDES
#include <map>
#include <vector>

// IBTK INCLUDES
#include <ibtk/LNodeInitStrategy.h>
#include <ibtk/LNodeIndex.h>
#include <ibtk/LNodeIndexVariable.h>

// PETSc INCLUDES
#include <petscvec.h>
#include <petscao.h>

// SAMRAI INCLUDES
#include <BoxArray.h>
#include <CartesianGridGeometry.h>
#include <CellIndex.h>
#include <CellVariable.h>
#include <CoarsenAlgorithm.h>
#include <CoarsenSchedule.h>
#include <ComponentSelector.h>
#include <Index.h>
#include <IntVector.h>
#include <LoadBalancer.h>
#include <PatchHierarchy.h>
#include <PatchLevel.h>
#include <RefineAlgorithm.h>
#include <RefineSchedule.h>
#include <StandardTagAndInitStrategy.h>
#include <VariableContext.h>
#include <VisItDataWriter.h>
#include <tbox/Database.h>
#include <tbox/Pointer.h>
#include <tbox/Serializable.h>

/////////////////////////////// FORWARD DECLARATIONS /////////////////////////

namespace IBTK
{
class LNodeIndexSet;
class LNodeLevelData;
class LagSiloDataWriter;
#if (NDIM == 3)
class LagM3DDataWriter;
#endif
}// namespace IBTK

/////////////////////////////// CLASS DEFINITION /////////////////////////////

namespace IBTK
{
/*!
 * \brief Class LDataManager coordinates the irregular distribution of
 * LNodeIndexData and LNodeLevelData on the patch hierarchy.
 *
 * The manager class is responsible for maintaining this data distribution and
 * for all inter-processor communications.  All access to instantiated
 * LDataManager objects is via the static method getManager().
 *
 * \note Multiple LDataManager objects may be instantiated simultaneously.
 */
class LDataManager
    : public SAMRAI::tbox::Serializable,
      public SAMRAI::mesh::StandardTagAndInitStrategy<NDIM>
{
public:
    /*!
     * The name of the LNodeLevelData that specifies the current positions of
     * the curvilinear mesh nodes.
     */
    static const std::string POSN_DATA_NAME;

    /*!
     * The name of the LNodeLevelData that specifies the initial positions of
     * the curvilinear mesh nodes.
     */
    static const std::string INIT_POSN_DATA_NAME;

    /*!
     * The name of the LNodeLevelData that specifies the velocities of the
     * curvilinear mesh nodes.
     */
    static const std::string VEL_DATA_NAME;

    /*!
     * Return a pointer to the instance of the Lagrangian data manager
     * corresponding to the specified name.  Access to LDataManager objects is
     * mediated by the getManager() function.
     *
     * Note that when a manager is accessed for the first time, the
     * freeAllManagers static method is registered with the ShutdownRegistry
     * class.  Consequently, all allocated managers are freed at program
     * completion.  Thus, users of this class do not explicitly allocate or
     * deallocate the LDataManager instances.
     *
     * \return A pointer to the data manager instance.
     *
     * \note By default, the ghost cell width is set according to the
     * interpolation and spreading weighting functions.
     */
    static LDataManager*
    getManager(
        const std::string& name,
        const std::string& interp_weighting_fcn,
        const std::string& spread_weighting_fcn,
        const SAMRAI::hier::IntVector<NDIM>& ghost_cell_width=SAMRAI::hier::IntVector<NDIM>(-1),
        bool register_for_restart=true);

    /*!
     * Deallocate all of the LDataManager instances.
     *
     * It is not necessary to call this function at program termination since it
     * is automatically called by the ShutdownRegistry class.
     */
    static void
    freeAllManagers();

    /*!
     * \name Methods to set the hierarchy and range of levels.
     */
    //\{

    /*!
     * \brief Reset patch hierarchy over which operations occur.
     */
    void
    setPatchHierarchy(
        SAMRAI::tbox::Pointer<SAMRAI::hier::PatchHierarchy<NDIM> > hierarchy);

    /*!
     * \brief Reset range of patch levels over which operations occur.
     *
     * The levels must exist in the hierarchy or an assertion failure will
     * result.
     */
    void
    resetLevels(
        const int coarsest_ln,
        const int finest_ln);

    //\}

    /*!
     * \brief Return the ghost cell width associated with the interaction
     * scheme.
     */
    const SAMRAI::hier::IntVector<NDIM>&
    getGhostCellWidth() const;

    /*!
     * \brief Return the weighting function associated with the
     * Eulerian-to-Lagrangian interpolation scheme.
     */
    const std::string&
    getInterpWeightingFunction() const;

    /*!
     * \brief Return the weighting function associated with the
     * Lagrangian-to-Eulerian spreading scheme.
     */
    const std::string&
    getSpreadWeightingFunction() const;

    /*!
     * \brief Spread a quantity from the Lagrangian mesh to the Eulerian grid.
     *
     * \note This spreading operation does include the scale factor
     * corresponding to the curvilinear volume element (dq dr ds).  The
     * spreading formula is
     *
     *     f(i,j,k) = Sum_{q,r,s} F(q,r,s) delta_h(x(i,j,k) - X(q,r,s)) ds(q,r,s)
     *
     * This is the standard regularized delta function spreading operation,
     * which spreads densities, \em NOT values.
     */
    void
    spread(
        const int f_data_idx,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& F_data,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& X_data,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& ds_data,
        const bool F_data_ghost_node_update=true,
        const bool X_data_ghost_node_update=true,
        const bool ds_data_ghost_node_update=true,
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Spread a quantity from the Lagrangian mesh to the Eulerian grid.
     *
     * \note This spreading operation does NOT include the scale factor
     * corresponding to the curvilinear volume element (dq dr ds).  The
     * spreading formula is
     *
     *     f(i,j,k) = Sum_{q,r,s} F(q,r,s) delta_h(x(i,j,k) - X(q,r,s))
     *
     * Unlike the standard regularized delta function spreading operation, the
     * implemented operation spreads values, \em NOT densities.
     */
    void
    spread(
        const int f_data_idx,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& F_data,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& X_data,
        const bool F_data_ghost_node_update=true,
        const bool X_data_ghost_node_update=true,
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Interpolate a quantity from the Eulerian grid to the Lagrangian
     * mesh.
     */
    void
    interp(
        const int f_data_idx,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& F_data,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& X_data,
        std::vector<SAMRAI::tbox::Pointer<SAMRAI::xfer::RefineSchedule<NDIM> > > f_refine_scheds=std::vector<SAMRAI::tbox::Pointer<SAMRAI::xfer::RefineSchedule<NDIM> > >(),
        const double fill_data_time=0.0,
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Interpolate a quantity from the Eulerian grid to the Lagrangian
     * mesh.
     *
     * \note This method is deprecated.  It should be replaced by calls to
     * interp().
     */
    void
    interpolate(
        const int f_data_idx,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& F_data,
        std::vector<SAMRAI::tbox::Pointer<LNodeLevelData> >& X_data,
        std::vector<SAMRAI::tbox::Pointer<SAMRAI::xfer::RefineSchedule<NDIM> > > f_refine_scheds=std::vector<SAMRAI::tbox::Pointer<SAMRAI::xfer::RefineSchedule<NDIM> > >(),
        const double fill_data_time=0.0,
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * Register a concrete strategy object with the integrator that specifies
     * the initial configuration of the curvilinear mesh nodes.
     */
    void
    registerLNodeInitStrategy(
        SAMRAI::tbox::Pointer<LNodeInitStrategy> lag_init);

    /*!
     * Free the concrete initialization strategy object.
     *
     * \note Be sure to call this method only once the initialization object is
     * no longer needed.
     */
    void
    freeLNodeInitStrategy();

    /*!
     * \brief Register a VisIt data writer with the manager.
     */
    void
    registerVisItDataWriter(
        SAMRAI::tbox::Pointer<SAMRAI::appu::VisItDataWriter<NDIM> > visit_writer);

    /*!
     * \brief Register a Silo data writer with the manager.
     */
    void
    registerLagSiloDataWriter(
        SAMRAI::tbox::Pointer<LagSiloDataWriter> silo_writer);

#if (NDIM == 3)
    /*!
     * \brief Register a myocardial3D data writer with the manager.
     */
    void
    registerLagM3DDataWriter(
        SAMRAI::tbox::Pointer<LagM3DDataWriter> m3D_writer);
#endif

    /*!
     * \brief Register a load balancer for non-uniform load balancing.
     */
    void
    registerLoadBalancer(
        SAMRAI::tbox::Pointer<SAMRAI::mesh::LoadBalancer<NDIM> > load_balancer);

    /*!
     * \brief Indicates whether there is Lagrangian data on the given patch
     * hierarchy level.
     */
    bool
    levelContainsLagrangianData(
        const int level_number) const;

    /*!
     * \return The number of total nodes of the Lagrangian data for the
     * specified level of the patch hierarchy.
     */
    int
    getNumberOfNodes(
        const int level_number) const;

    /*!
     * \return The number of local (i.e., on processor) nodes of the Lagrangian
     * data for the specified level of the patch hierarchy.
     *
     * \note This count does not include nodes that only lie in ghost cells for
     * the current process.
     */
    int
    getNumberOfLocalNodes(
        const int level_number) const;

    /*!
     * \return The number of nodes on all processors with MPI rank less than the
     * current process on the specified level of the patch hierarchy.
     *
     * \note This count does not include nodes that only lie in ghost cells for
     * the current process.
     */
    int
    getGlobalNodeOffset(
        const int level_number) const;

    /*!
     * \brief Get the specified Lagrangian quantity data on the given patch
     * hierarchy level.
     */
    SAMRAI::tbox::Pointer<LNodeLevelData>
    getLNodeLevelData(
        const std::string& quantity_name,
        const int level_number);

    /*!
     * \brief Allocate new Lagrangian level data with the specified name and
     * depth.  If specified, the quantity is maintained as the patch hierarchy
     * evolves.
     *
     * \note Quantities maintained by the LDataManager must have unique names.
     * The name "X" is reserved for the nodal coordinates.
     */
    SAMRAI::tbox::Pointer<LNodeLevelData>
    createLNodeLevelData(
        const std::string& quantity_name,
        const int level_number,
        const int depth=1,
        const bool maintain_data=false);

    /*!
     * \brief Get the patch data descriptor index for the Lagrangian index data.
     */
    int
    getLNodeIndexPatchDescriptorIndex() const;

    /*!
     * \brief Get the patch data descriptor index for the workload cell data.
     */
    int
    getWorkloadPatchDescriptorIndex() const;

    /*!
     * \brief Get the patch data descriptor index for the Lagrangian node count
     * cell data.
     */
    int
    getNodeCountPatchDescriptorIndex() const;

    /*!
     * \brief Get the patch data descriptor index for the irregular Cartesian
     * grid cell data.
     */
    int
    getIrregularCellPatchDescriptorIndex() const;

    /*!
     * \brief Get the patch data descriptor index for the MPI process mapping
     * cell data.
     */
    int
    getProcMappingPatchDescriptorIndex() const;

    /*!
     * \brief Get a list of Lagrangian structure names for the specified level
     * of the patch hierarchy.
     */
    std::vector<std::string>
    getLagrangianStructureNames(
        const int level_number) const;

    /*!
     * \brief Get a list of Lagrangian structure IDs for the specified level of
     * the patch hierarchy.
     */
    std::vector<int>
    getLagrangianStructureIDs(
        const int level_number) const;

    /*!
     * \brief Get the ID of the Lagrangian structure associated with the
     * specified Lagrangian index.
     *
     * \note Returns -1 in the case that the Lagrangian index is not associated
     * with any Lagrangian structure.
     */
    int
    getLagrangianStructureID(
        const int lagrangian_index,
        const int level_number) const;

    /*!
     * \brief Get the ID of the Lagrangian structure with the specified name.
     *
     * \note Returns -1 in the case that the Lagrangian structure name is not
     * associated with any Lagrangian structure.
     */
    int
    getLagrangianStructureID(
        const std::string& structure_name,
        const int level_number) const;

    /*!
     * \brief Get the name of the Lagrangian structure with the specified ID.
     *
     * \note Returns "UNKNOWN" in the case that the Lagrangian structure ID is
     * not associated with any Lagrangian structure.
     */
    std::string
    getLagrangianStructureName(
        const int structure_id,
        const int level_number) const;

    /*!
     * \brief Get the range of Lagrangian indices for the Lagrangian structure
     * with the specified ID.
     *
     * \return A pair of indices such that if pair.first <= lag_idx <
     * pair.second, then lag_idx is associated with the specified structure;
     * otherwise, lag_idx is not associated with the specified structure.
     *
     * \note Returns std::make_pair(-1,-1) in the case that the Lagrangian
     * structure ID is not associated with any Lagrangian structure.
     */
    std::pair<int,int>
    getLagrangianStructureIndexRange(
        const int structure_id,
        const int level_number) const;

    /*!
     * \brief Get the center of mass of the Lagrangian structure with the
     * specified ID.
     *
     * \note The center of mass X of a particular structure is computed as
     *
     *    X = (1/N) Sum_{k in structure} X_k
     *
     * in which N is the number of nodes associated with that structure.
     *
     * \note Returns std::vector<double>(NDIM,0.0) in the case that the
     * Lagrangian structure ID is not associated with any Lagrangian structure.
     */
    std::vector<double>
    getLagrangianStructureCenterOfMass(
        const int structure_id,
        const int level_number) const;

    /*!
     * \brief Get the bounding box of the Lagrangian structure with the
     * specified ID.
     *
     * \note Returns the entire range of double precision values in the case
     * that the Lagrangian structure ID is not associated with any Lagrangian
     * structure.
     */
    std::pair<std::vector<double>,std::vector<double> >
    getLagrangianStructureBoundingBox(
        const int structure_id,
        const int level_number) const;

    /*!
     * \brief Reset the positions of the nodes of the Lagrangian structure with
     * the specified ID to be equal to the initial positions but shifted so that
     * the bounding box of the structure is centered about X_center.
     *
     * \note This operation must be performed immediately before a regridding
     * operation, otherwise the results are undefined.
     */
    void
    reinitLagrangianStructure(
        const std::vector<double>& X_center,
        const int structure_id,
        const int level_number);

    /*!
     * \brief Shift the positions of the nodes of the Lagrangian structure with
     * the specified ID by a displacement dX.
     *
     * \note This operation must be performed immediately before a regridding
     * operation, otherwise the results are undefined.
     *
     * \warning All displacements must involve shifts that do \em not cross
     * periodic boundaries.
     */
    void
    displaceLagrangianStructure(
        const std::vector<double>& dX,
        const int structure_id,
        const int level_number);

    /*!
     * \brief Activate the Lagrangian structures with the specified ID numbers.
     *
     * \note This method is collective (i.e., must be called by all MPI
     * processes); however, each MPI process may provide a different collection
     * of structures to activate.
     */
    void
    activateLagrangianStructures(
        const std::vector<int>& structure_ids,
        const int level_number);

    /*!
     * \brief Inactivate the Lagrangian structures with the specified ID
     * numbers.
     *
     * \note This method is collective (i.e., must be called by all MPI
     * processes); however, each MPI process may provide a different collection
     * of structures to inactivate.
     */
    void
    inactivateLagrangianStructures(
        const std::vector<int>& structure_ids,
        const int level_number);

    /*!
     * \brief Determine whether the Lagrangian structure with the specified ID
     * number is activated.
     */
    bool
    getLagrangianStructureIsActivated(
        const int structure_id,
        const int level_number) const;

    /*!
     * \brief Set the components of the supplied LNodeLevelData object to zero
     * for those entries that correspond to inactivated structures.
     */
    void
    zeroInactivatedComponents(
        SAMRAI::tbox::Pointer<LNodeLevelData> lag_data,
        const int level_number) const;

    /*!
     * \brief Map the collection of Lagrangian indices to the corresponding
     * global PETSc indices.
     */
    void
    mapLagrangianToPETSc(
        std::vector<int>& inds,
        const int level_number) const;

    /*!
     * \brief Map the collection of global PETSc indices to the corresponding
     * Lagrangian indices.
     */
    void
    mapPETScToLagrangian(
        std::vector<int>& inds,
        const int level_number) const;

    /*!
     * \brief Scatter data from the Lagrangian ordering to the global PETSc
     * ordering.
     *
     * \todo Optimize the implementation of this method.
     */
    void
    scatterLagrangianToPETSc(
        Vec& lagrangian_vec,
        Vec& petsc_vec,
        const int level_number) const;

    /*!
     * \brief Scatter data from the global PETSc ordering to the Lagrangian
     * ordering.
     *
     * \todo Optimize the implementation of this method.
     */
    void
    scatterPETScToLagrangian(
        Vec& petsc_vec,
        Vec& lagrangian_vec,
        const int level_number) const;

    /*!
     * \brief Scatter data from a distributed PETSc vector to all processors.
     *
     * \todo Optimize the implementation of this method.
     */
    void
    scatterToAll(
        Vec& parallel_vec,
        Vec& sequential_vec) const;

    /*!
     * \brief Scatter data from a distributed PETSc vector to processor zero.
     *
     * \todo Optimize the implementation of this method.
     */
    void
    scatterToZero(
        Vec& parallel_vec,
        Vec& sequential_vec) const;

    /*!
     * \brief Start the process of redistributing the Lagrangian data.
     *
     * This method uses the present location of each Lagrangian mesh node to
     * redistribute the LNodeIndexData managed by this object.
     *
     * \note This routine assumes that the time interval between node
     * redistribution satisfies a timestep restriction of the form dt <=
     * C*dx*|U| with C <= 1.  This restriction prevents nodes from moving more
     * than one cell width per timestep.
     *
     * \see endDataRedistribution
     */
    void
    beginDataRedistribution(
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Finish the process of redistributing the Lagrangian data.
     *
     * This method redistributes the quantities associated with each node in the
     * Lagrangian mesh according to the data distribution defined by the
     * LNodeIndexData managed by this object.  This routine potentially
     * involves SUBSTANTIAL inter-processor communication.
     *
     * \note Since this routine potentially results in a large amount of
     * inter-processor communication, it may be worth putting it off for as long
     * as possible.  If the timestep dt satisfies a condition of the form dt <=
     * C*dx*|U| with C << 1, it may be possible to redistribute the Lagrangian
     * data less frequently than every timestep.
     *
     * \see beginDataRedistribution
     */
    void
    endDataRedistribution(
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Update the workload and count of nodes per cell.
     *
     * This routine updates cell data that is maintained on the patch hierarchy
     * to track the number of nodes in each cell of the AMR index space.  The
     * node count data is used to tag cells for refinement, and to specify
     * non-uniform load balancing.  The workload per cell is defined by
     *
     *    workload(i) = alpha_work + beta_work*node_count(i)
     *
     * in which alpha and beta are parameters that each default to the value 1.
     */
    void
    updateWorkloadData(
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Update the irregular cell data.
     *
     * This routine updates cell data that is maintained on the patch hierarchy
     * to track "irregular" grid cells that lie within the support of the
     * regularized delta function.
     */
    void
    updateIrregularCellData(
        const int stencil_size,
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Each LNodeIndex object owns a pointer to its nodal location.  This
     * routine updates these pointers based on the current state of the
     * Lagrangian nodal position data.
     *
     * \note It is important to note that any operation on the LNodeLevelData
     * that results in the restoration of the local form of the underlying PETSc
     * Vec object has the potential to invalidate these pointers.
     */
    void
    restoreLocationPointers(
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief Each LNodeIndex object owns a pointer to its nodal location.  This
     * routine invalidates these pointers, an action that is mainly useful for
     * debugging purposes.
     */
    void
    invalidateLocationPointers(
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    ///
    ///  The following routines:
    ///
    ///      initializeLevelData(),
    ///      resetHierarchyConfiguration(),
    ///      applyGradientDetector()
    ///
    ///  are concrete implementations of functions declared in the
    ///  SAMRAI::mesh::StandardTagAndInitStrategy abstract base class.
    ///

    /*!
     * Initialize data on a new level after it is inserted into an AMR patch
     * hierarchy by the gridding algorithm.  The level number indicates that of
     * the new level.  The old_level pointer corresponds to the level that
     * resided in the hierarchy before the level with the specified number was
     * introduced.  If the pointer is null, there was no level in the hierarchy
     * prior to the call and the level data is set based on the user routines
     * and the simulation time.  Otherwise, the specified level replaces the old
     * level and the new level receives data from the old level appropriately
     * before it is destroyed.
     *
     * The boolean argument initial_time indicates whether the level is being
     * introduced for the first time (i.e., at initialization time) or after
     * some regrid process during the calculation beyond the initial hierarchy
     * construction.  This information is provided since the initialization of
     * the data on a patch may be different in each of those circumstances.  The
     * can_be_refined boolean argument indicates whether the level is the finest
     * level allowed in the hierarchy.  This may or may not affect the data
     * initialization process depending on the problem.
     *
     * When assertion checking is active, an unrecoverable exception will result
     * if the hierarchy pointer is null, the level number does not match any
     * level in the hierarchy, or the old level number does not match the level
     * number (if the old level pointer is non-null).
     */
    void
    initializeLevelData(
        const SAMRAI::tbox::Pointer<SAMRAI::hier::BasePatchHierarchy<NDIM> > hierarchy,
        const int level_number,
        const double init_data_time,
        const bool can_be_refined,
        const bool initial_time,
        const SAMRAI::tbox::Pointer<SAMRAI::hier::BasePatchLevel<NDIM> > old_level=SAMRAI::tbox::Pointer<SAMRAI::hier::BasePatchLevel<NDIM> >(NULL),
        const bool allocate_data=true);

    /*!
     * Reset cached communication schedules after the hierarchy has changed (for
     * example, due to regridding) and the data has been initialized on the new
     * levels.  The intent is that the cost of data movement on the hierarchy
     * will be amortized across multiple communication cycles, if possible.  The
     * level numbers indicate the range of levels in the hierarchy that have
     * changed.  However, this routine updates communication schedules every
     * level finer than and including that indexed by the coarsest level number
     * given.
     *
     * When assertion checking is active, an unrecoverable exception will result
     * if the hierarchy pointer is null, any pointer to a level in the hierarchy
     * that is coarser than the finest level is null, or the given level numbers
     * not specified properly; e.g., coarsest_ln > finest_ln.
     */
    void
    resetHierarchyConfiguration(
        const SAMRAI::tbox::Pointer<SAMRAI::hier::BasePatchHierarchy<NDIM> > hierarchy,
        const int coarsest_ln,
        const int finest_ln);

    /*!
     * Set integer tags to "one" in cells where refinement of the given level
     * should occur due to the presence of Lagrangian data.  The double time
     * argument is the regrid time.  The integer "tag_index" argument is the
     * patch descriptor index of the cell centered integer tag array on each
     * patch in the hierarchy.  The boolean argument initial_time indicates
     * whether the level is being subject to refinement at the initial
     * simulation time.  If it is false, then the error estimation process is
     * being invoked at some later time after the AMR hierarchy was initially
     * constructed.  The boolean argument uses_richardson_extrapolation_too is
     * true when Richardson extrapolation error estimation is used in addition
     * to the gradient detector, and false otherwise.  This argument helps the
     * user to manage multiple regridding criteria.
     *
     * When assertion checking is active, an unrecoverable exception will result
     * if the hierarchy pointer is null or the level number does not match any
     * existing level in the hierarchy.
     */
    virtual void
    applyGradientDetector(
        const SAMRAI::tbox::Pointer<SAMRAI::hier::BasePatchHierarchy<NDIM> > hierarchy,
        const int level_number,
        const double error_data_time,
        const int tag_index,
        const bool initial_time,
        const bool uses_richardson_extrapolation_too);

    ///
    ///  The following routines:
    ///
    ///      putToDatabase()
    ///
    ///  are concrete implementations of functions declared in the
    ///  SAMRAI::tbox::Serializable abstract base class.
    ///

    /*!
     * Write out object state to the given database.
     *
     * When assertion checking is active, database pointer must be non-null.
     */
    void
    putToDatabase(
        SAMRAI::tbox::Pointer<SAMRAI::tbox::Database> db);

protected:
    /*!
     * \brief Constructor.
     */
    LDataManager(
        const std::string& object_name,
        const std::string& interp_weighting_fcn,
        const std::string& spread_weighting_fcn,
        const SAMRAI::hier::IntVector<NDIM>& ghost_width,
        bool register_for_restart=true);

    /*!
     * \brief The LDataManager destructor cleans up any remaining PETSc AO
     * objects.
     */
    ~LDataManager();

private:
    /*!
     * \brief Default constructor.
     *
     * \note This constructor is not implemented and should not be used.
     */
    LDataManager();

    /*!
     * \brief Copy constructor.
     *
     * \note This constructor is not implemented and should not be used.
     *
     * \param from The value to copy to this object.
     */
    LDataManager(
        const LDataManager& from);

    /*!
     * \brief Assignment operator.
     *
     * \note This operator is not implemented and should not be used.
     *
     * \param that The value to assign to this object.
     *
     * \return A reference to this object.
     */
    LDataManager&
    operator=(
        const LDataManager& that);

    /*!
     * \brief Common implementation of scatterPETScToLagrangian() and
     * scatterLagrangianToPetsc().
     */
    void
    scatterData(
        Vec& lagrangian_vec,
        Vec& petsc_vec,
        const int level_number,
        ScatterMode mode) const;

    /*!
     * \brief Begin the process of refilling nonlocal Lagrangian quantities over
     * the specified range of levels in the patch hierarchy.
     *
     * The operation is essentially equivalent to refilling ghost cells for
     * structured (SAMRAI native) data.
     */
    void
    beginNonlocalDataFill(
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * \brief End the process of refilling nonlocal Lagrangian quantities over
     * the specified range of levels in the patch hierarchy.
     *
     * The operation is essentially equivalent to refilling ghost cells for
     * structured (SAMRAI native) data.
     */
    void
    endNonlocalDataFill(
        const int coarsest_ln=-1,
        const int finest_ln=-1);

    /*!
     * Determines the global Lagrangian and PETSc indices of the local and
     * nonlocal nodes associated with the processor as well as the local PETSc
     * indices of the interior and ghost nodes in each patch of the specified
     * level.
     *
     * \note The set of local Lagrangian indices lists all the nodes that are
     * owned by this processor.  The set of nonlocal Lagrangian indices lists
     * all of the nodes that are not owned by this processor but that appear in
     * the ghost cell region of some patch on this processor.  Both of these
     * sets of node indices use the fixed, global Lagrangian indexing scheme.
     *
     * \note The set of interior local indices lists the nodes that live on the
     * interior on each patch.  The set of ghost local indices lists the nodes
     * that live in the ghost cell region of each patch.  Both of these sets of
     * node indices use the local PETSc indexing scheme, determined by the
     * present distribution of data across the processors.
     *
     * Since each processor may own multiple patches in a given level, nodes
     * appearing in the ghost cell region of a patch may or may not be owned by
     * this processor.
     */
    int
    computeNodeDistribution(
        std::vector<int>& local_lag_indices,
        std::vector<int>& nonlocal_lag_indices,
        AO& ao,
        std::vector<int>& local_petsc_indices,
        std::vector<int>& nonlocal_petsc_indices,
        int& num_nodes,
        int& node_offset,
        std::map<int,std::vector<int>*>& patch_interior_local_indices,
        std::map<int,std::vector<int>*>& patch_ghost_local_indices,
        const int level_number);

    /*!
     * Determine the number of local Lagrangian nodes on all MPI processes with
     * rank less than the rank of the current MPI process.
     */
    static void
    computeNodeOffsets(
        int& num_nodes,
        int& node_offset,
        const int& num_local_nodes);

    /*!
     * Read object state from the restart file and initialize class data
     * members.  The database from which the restart data is read is determined
     * by the object_name specified in the constructor.
     *
     * Unrecoverable Errors:
     *
     *    -   The database corresponding to object_name is not found in the
     *        restart file.
     *
     *    -   The class version number and restart version number do not match.
     *
     */
    void
    getFromRestart();

    /*!
     * Static data members used to control access to and destruction of
     * singleton data manager instance.
     */
    static std::map<std::string,LDataManager*> s_data_manager_instances;
    static bool s_registered_callback;
    static unsigned char s_shutdown_priority;

    /*
     * The object name is used as a handle to databases stored in restart files
     * and for error reporting purposes.  The boolean is used to control restart
     * file writing operations.
     */
    std::string d_object_name;
    bool d_registered_for_restart;

    /*
     * Grid hierarchy information.
     */
    SAMRAI::tbox::Pointer<SAMRAI::hier::PatchHierarchy<NDIM> > d_hierarchy;
    SAMRAI::tbox::Pointer<SAMRAI::geom::CartesianGridGeometry<NDIM> > d_grid_geom;
    int d_coarsest_ln, d_finest_ln;

    /*
     * We cache a pointer to the visualization data writers to register plot
     * variables.
     */
    SAMRAI::tbox::Pointer<SAMRAI::appu::VisItDataWriter<NDIM> > d_visit_writer;
    SAMRAI::tbox::Pointer<LagSiloDataWriter> d_silo_writer;
#if (NDIM == 3)
    SAMRAI::tbox::Pointer<LagM3DDataWriter> d_m3D_writer;
#endif

    /*
     * We cache a pointer to the load balancer.
     */
    SAMRAI::tbox::Pointer<SAMRAI::mesh::LoadBalancer<NDIM> > d_load_balancer;

    /*
     * Objects used to specify and initialize the Lagrangian data on the patch
     * hierarchy.
     */
    SAMRAI::tbox::Pointer<LNodeInitStrategy> d_lag_init;
    std::vector<bool> d_level_contains_lag_data;

    /*
     * SAMRAI::hier::Variable pointer and patch data descriptor indices for the
     * LNodeIndexData used to define the data distribution.
     */
    SAMRAI::tbox::Pointer<LNodeIndexVariable> d_lag_node_index_var;
    int d_lag_node_index_current_idx, d_lag_node_index_scratch_idx;

    /*
     * SAMRAI::hier::Variable pointer and patch data descriptor indices for the
     * cell variable used to determine the workload for nonuniform load
     * balancing.
     */
    double d_alpha_work, d_beta_work;
    SAMRAI::tbox::Pointer<SAMRAI::pdat::CellVariable<NDIM,double> > d_workload_var;
    int d_workload_idx;
    bool d_output_workload;

    /*
     * SAMRAI::hier::Variable pointer and patch data descriptor indices for the
     * cell variable used to keep track of the count of the nodes in each cell
     * for visualization and tagging purposes.
     */
    SAMRAI::tbox::Pointer<SAMRAI::pdat::CellVariable<NDIM,double> > d_node_count_var;
    int d_node_count_idx;
    bool d_output_node_count;

    /*
     * SAMRAI::hier::Variable pointer and patch data descriptor indices for the
     * cell variable used to indicate the "irregular" Cartesian grid cells,
     * i.e., those Cartesian grid cells within the stencil of the regularized
     * delta function centered about a node of the Lagrangian mesh.
     */
    SAMRAI::tbox::Pointer<SAMRAI::pdat::CellVariable<NDIM,double> > d_irregular_cell_var;
    int d_irregular_cell_idx;

    /*
     * SAMRAI::hier::Variable pointer and patch data descriptor indices for the
     * cell variable used to keep track of the MPI process assigned to each
     * patch.
     */
    SAMRAI::tbox::Pointer<SAMRAI::pdat::CellVariable<NDIM,int> > d_mpi_proc_var;
    int d_mpi_proc_idx;
    bool d_output_mpi_proc;

    /*
     * The weighting functions used to mediate Lagrangian-Eulerian interaction.
     */
    const std::string d_interp_weighting_fcn;
    const std::string d_spread_weighting_fcn;

    /*
     * SAMRAI::hier::IntVector object that determines the ghost cell width of
     * the LNodeIndexData SAMRAI::hier::PatchData objects.
     */
    const SAMRAI::hier::IntVector<NDIM> d_ghost_width;

    /*
     * Communications algorithms and schedules.
     */
    SAMRAI::tbox::Pointer<SAMRAI::xfer::RefineAlgorithm<NDIM> > d_lag_node_index_bdry_fill_alg;
    std::vector<SAMRAI::tbox::Pointer<SAMRAI::xfer::RefineSchedule<NDIM> > > d_lag_node_index_bdry_fill_scheds;

    SAMRAI::tbox::Pointer<SAMRAI::xfer::CoarsenAlgorithm<NDIM> > d_node_count_coarsen_alg;
    std::vector<SAMRAI::tbox::Pointer<SAMRAI::xfer::CoarsenSchedule<NDIM> > > d_node_count_coarsen_scheds;

    /*
     * SAMRAI::hier::VariableContext objects are used for data management.
     */
    SAMRAI::tbox::Pointer<SAMRAI::hier::VariableContext> d_current_context, d_scratch_context;

    /*
     * ComponenetSelector object allow for the collective allocation and
     * deallocation of SAMRAI::hier::PatchData.
     */
    SAMRAI::hier::ComponentSelector d_current_data, d_scratch_data;

    /*!
     * \name Data that is separately maintained for each level of the patch
     * hierarchy.
     */
    //\{

    /*!
     * Information about the names and IDs of the various Lagrangian structures.
     */
    std::vector<std::map<std::string,int> > d_strct_name_to_strct_id_map;
    std::vector<std::map<int,std::string> > d_strct_id_to_strct_name_map;
    std::vector<std::map<int,std::pair<int,int> > > d_strct_id_to_lag_idx_range_map;
    std::vector<std::map<int,int> > d_last_lag_idx_to_strct_id_map;
    std::vector<std::map<int,bool> > d_strct_activation_map;
    std::vector<std::vector<int> > d_displaced_strct_ids;
    std::vector<std::vector<std::pair<std::vector<double>,std::vector<double> > > > d_displaced_strct_bounding_boxes;
    std::vector<std::vector<SAMRAI::tbox::Pointer<LNodeIndex> > > d_displaced_strct_lnode_idxs;
    std::vector<std::vector<std::vector<double> > > d_displaced_strct_lnode_posns;

    /*!
     * The Lagrangian quantity data owned by the manager object.
     */
    std::vector<std::map<std::string,SAMRAI::tbox::Pointer<LNodeLevelData> > > d_lag_quantity_data;

    /*!
     * Indicates whether the LNodeLevelData is in synch with the
     * LNodeIndexData.
     */
    std::vector<bool> d_needs_synch;

    /*!
     * PETSc AO objects provide mappings between the fixed global Lagrangian
     * node IDs and the ever-changing global PETSc ordering.
     */
    std::vector<AO> d_ao;
    static std::vector<int> s_ao_dummy;

    /*!
     * The total number of nodes for all processors.
     */
    std::vector<int> d_num_nodes;

    /*!
     * The total number of local nodes for all processors with rank less than
     * the rank of the current processor.
     */
    std::vector<int> d_node_offset;

    /*!
     * The Lagrangian node indices of all local and nonlocal nodes on each level
     * of the patch hierarchy.
     *
     * A local node is one that is owned by a patch on this processor, while a
     * nonlocal node is one that is owned by a patch on another processor, but
     * found in the ghost region of some patch owned by this processor.
     *
     * Note that these sets of indices provide the information necessary to
     * determine the local PETSc index for all nodes.  Local node
     * d_local_lag_indices[ln][j] has local PETSc index j, while nonlocal node
     * d_nonlocal_lag_indices[ln][k] has local PETSc index
     * d_local_lag_indices.size()+j.
     *
     * It is possible to determine the global PETSc index of a local node by
     * making use of d_node_offset.  Local node d_local_lag_indices[ln][j] has
     * global PETSc index j+d_node_offset[ln].  A similar mapping for nonlocal
     * nodes is not well defined.
     */
    std::vector<std::vector<int> > d_local_lag_indices;
    std::vector<std::vector<int> > d_nonlocal_lag_indices;

    /*!
     * The node indices of all local nodes (i.e. the nodes owned by this
     * processor) on each level of the hierarchy.  The indices are in the global
     * PETSc ordering corresponding to a depth of 1.
     */
    std::vector<std::vector<int> > d_local_petsc_indices;

    /*!
     * The node indices of all nonlocal nodes (i.e. the nodes owned by another
     * processor that appear in the ghost region of some patch owned by this
     * processor) on each level of the hierarchy.  The indices are in the global
     * PETSc ordering corresponding to a depth of 1.
     *
     * \note These sets are used to create the VecScatter objects used to
     * transfer data from the old PETSc ordering to the new PETSc ordering.
     * Since the ordering is different for different depths of LNodeLevelData,
     * we compute one set of indices for each depth that is being reordered.
     */
    std::vector<std::map<int,std::vector<int> > > d_nonlocal_petsc_indices;

    //\}
};
}// namespace IBTK

/////////////////////////////// INLINE ///////////////////////////////////////

#include <ibtk/LDataManager.I>

//////////////////////////////////////////////////////////////////////////////

#endif //#ifndef included_LDataManager
