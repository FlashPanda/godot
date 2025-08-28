# 子级 CMakeLists.txt：不声明 project()，作为 modules/jolt 的构建脚本

cmake_minimum_required(VERSION 3.18)

# ----- 选项开关，对应 SCons 的 env.* -----
# 是否为开发构建（对应 env.dev_build）：启用 JPH_ENABLE_ASSERTS
option(DEV_BUILD "对应 env.dev_build，启用断言" ON)
# 是否为编辑器构建（对应 env.editor_build）：启用 JPH_DEBUG_RENDERER
option(EDITOR_BUILD "对应 env.editor_build，启用 Debug Renderer" ON)
# 精度控制（对应 env[\"precision\"]）
set(PRECISION "single" CACHE STRING "精度：single 或 double（对应 env[\"precision\"]）")
set_property(CACHE PRECISION PROPERTY STRINGS single double)

# ----- 第三方 Jolt Physics 源码 -----
set(JOLT_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/thirdparty/jolt_physics")

# 明确列出与 SCons 脚本一致的源文件清单
set(JOLT_THIRDPARTY_SOURCES
        "${JOLT_THIRDPARTY_DIR}/Jolt/RegisterTypes.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/AABBTree/AABBTreeBuilder.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/Color.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/Factory.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/IssueReporting.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/JobSystemSingleThreaded.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/JobSystemThreadPool.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/JobSystemWithBarrier.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/LinearCurve.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/Memory.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/Profiler.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/RTTI.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/Semaphore.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/StringTools.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Core/TickCounter.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Geometry/ConvexHullBuilder.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Geometry/ConvexHullBuilder2D.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Geometry/Indexify.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Geometry/OrientedBox.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Math/Vec3.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/ObjectStream/SerializableObject.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/DeterminismLog.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/IslandBuilder.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/LargeIslandSplitter.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/PhysicsScene.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/PhysicsSystem.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/PhysicsUpdateContext.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/StateRecorderImpl.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Body/Body.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Body/BodyCreationSettings.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Body/BodyInterface.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Body/BodyManager.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Body/MassProperties.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Body/MotionProperties.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Character/Character.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Character/CharacterBase.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Character/CharacterVirtual.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/CastConvexVsTriangles.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/CastSphereVsTriangles.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/CollideConvexVsTriangles.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/CollideSphereVsTriangles.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/CollisionDispatch.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/CollisionGroup.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/EstimateCollisionResponse.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/GroupFilter.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/GroupFilterTable.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/ManifoldBetweenTwoFaces.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/NarrowPhaseQuery.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/NarrowPhaseStats.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/PhysicsMaterial.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/PhysicsMaterialSimple.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/TransformedShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/BroadPhase/BroadPhase.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/BroadPhase/BroadPhaseBruteForce.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/BroadPhase/BroadPhaseQuadTree.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/BroadPhase/QuadTree.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/BoxShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/CapsuleShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/CompoundShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/ConvexHullShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/ConvexShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/CylinderShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/DecoratedShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/EmptyShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/HeightFieldShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/MeshShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/MutableCompoundShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/PlaneShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/RotatedTranslatedShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/ScaledShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/Shape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/SphereShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/StaticCompoundShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/TaperedCapsuleShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/TaperedCylinderShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Collision/Shape/TriangleShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/ConeConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/Constraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/ConstraintManager.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/ContactConstraintManager.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/DistanceConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/FixedConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/GearConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/HingeConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/MotorSettings.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/PathConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/PathConstraintPath.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/PathConstraintPathHermite.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/PointConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/PulleyConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/RackAndPinionConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/SixDOFConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/SliderConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/SpringSettings.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/SwingTwistConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Constraints/TwoBodyConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Ragdoll/Ragdoll.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/SoftBody/SoftBodyCreationSettings.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/SoftBody/SoftBodyMotionProperties.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/SoftBody/SoftBodyShape.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/SoftBody/SoftBodySharedSettings.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/MotorcycleController.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/TrackedVehicleController.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleAntiRollBar.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleConstraint.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleController.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleDifferential.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleEngine.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleTrack.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/VehicleTransmission.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/Wheel.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Physics/Vehicle/WheeledVehicleController.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Renderer/DebugRenderer.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Renderer/DebugRendererPlayback.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Renderer/DebugRendererRecorder.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Renderer/DebugRendererSimple.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Skeleton/SkeletalAnimation.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Skeleton/Skeleton.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Skeleton/SkeletonMapper.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/Skeleton/SkeletonPose.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleGrouper/TriangleGrouperClosestCentroid.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleGrouper/TriangleGrouperMorton.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleSplitter/TriangleSplitter.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleSplitter/TriangleSplitterBinning.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleSplitter/TriangleSplitterFixedLeafSize.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleSplitter/TriangleSplitterLongestAxis.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleSplitter/TriangleSplitterMean.cpp"
        "${JOLT_THIRDPARTY_DIR}/Jolt/TriangleSplitter/TriangleSplitterMorton.cpp"
)

# 用 OBJECT 库承载第三方源码，以便单独关闭警告
add_library(jolt_thirdparty_objs OBJECT ${JOLT_THIRDPARTY_SOURCES})
target_include_directories(jolt_thirdparty_objs PUBLIC "${JOLT_THIRDPARTY_DIR}")

# 根据开关追加宏
if(DEV_BUILD)
    target_compile_definitions(jolt_thirdparty_objs PUBLIC JPH_ENABLE_ASSERTS)
endif()
if(EDITOR_BUILD)
    target_compile_definitions(jolt_thirdparty_objs PUBLIC JPH_DEBUG_RENDERER)
endif()
if(PRECISION STREQUAL "double")
    target_compile_definitions(jolt_thirdparty_objs PUBLIC JPH_DOUBLE_PRECISION)
endif()

# 关闭第三方源码的编译警告（分别适配 MSVC 与 GCC/Clang）
if(MSVC)
    target_compile_options(jolt_thirdparty_objs PRIVATE /w)
else()
    target_compile_options(jolt_thirdparty_objs PRIVATE -w)
endif()

# ----- 本模块源码（Godot 源）-----
# 收集本目录下 *.cpp 以及子目录 joints/misc/objects/shapes/spaces 下的 *.cpp
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/joints/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/misc/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/objects/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/shapes/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/spaces/*.cpp"
)

# 生成静态库 module_jolt，供父级 modules/CMakeLists.txt 自动链接到 godot
add_library(module_jolt STATIC
        ${MODULE_LOCAL_SOURCES}
        $<TARGET_OBJECTS:jolt_thirdparty_objs>
)

target_include_directories(module_jolt
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${JOLT_THIRDPARTY_DIR}"
)

# 同步第三方的公共宏到最终库（包含到使用者）
if(DEV_BUILD)
    target_compile_definitions(module_jolt PUBLIC JPH_ENABLE_ASSERTS)
endif()
if(EDITOR_BUILD)
    target_compile_definitions(module_jolt PUBLIC JPH_DEBUG_RENDERER)
endif()
if(PRECISION STREQUAL "double")
    target_compile_definitions(module_jolt PUBLIC JPH_DOUBLE_PRECISION)
endif()

# 若父级已有目标 godot，也可在此直接追加（可选，通常由父级统一处理）
if(TARGET godot)
    target_link_libraries(godot PRIVATE module_jolt)
endif()