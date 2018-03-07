//
// Created by visionlab on 2/20/18.
//
#pragma once
#include "common/eigen_alias.h"

// stl
#include <unordered_map>
#include <memory>
#include <list>

// 3rd party
#include "folly/dynamic.h"
#include "Core/Core.h"

namespace feh {

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
// BASIC DATA STRUCTURE
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
/// \brief: Model data structure for annotation/evaluation tool.
struct Model {
    Model() :
        pcd_ptr_(nullptr) {
        model_to_scene_.setIdentity();
    }
    std::string model_name_;    // different from the instance name, WITHOUT INDEX in the scene
    Eigen::Matrix<double, Eigen::Dynamic, 3> V_;    // vertices
    Eigen::Matrix<int, Eigen::Dynamic, 3> F_;   // faces
    Eigen::Matrix<double, 4, 4> model_to_scene_;
    std::shared_ptr <three::PointCloud> pcd_ptr_;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
// ANNOTATION
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/// \brief: Annotation procedure. Automated tool to align models to scene.
void AnnotationTool(const folly::dynamic &config);

/// \brief: Enumerate possible azimuth rotation and perform ICP to find the best transformation to align the
/// model to the scene. Both model and scene are represented as a point cloud.
Eigen::Matrix4d RegisterModelToScene(std::shared_ptr <three::PointCloud> model,
                                     std::shared_ptr <three::PointCloud> scene,
                                     const folly::dynamic options);
/// \brief: Helper.
double MinY(const std::vector<Eigen::Vector3d> &v);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
// EVALUATION
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/// \brief: Evaluation procedure. Automated tool for reconstructed scene alignment and evaluation..
void EvaluationTool(const folly::dynamic &config);

/// \Brief: Find the rigid body transformation to align the two lists of objects.
/// Method: find two objects with same shape name in the tgt and src. Compute the relative
/// transformation (corvis -> elasticfusion). Apply this transformation to the rest of
/// objects and find the nearest object in the other list. For the transformation with most supports,
/// compute the optimal transformation with IRLS. (smoothing on tangent space with inverse residual
/// as weights).
/// \param tgt: Target object set.
/// \param src: Source object set.
three::RegistrationResult RegisterScenes(
    const std::unordered_map<int, Model> &tgt,
    const std::unordered_map<int, Model> &src);

/// \brief: Find correspondences given two lists of poses.
/// \param tgt: Target object set.
/// \param src: Source object set.
/// \param T_tgt_src: The proposed transformation from src to target (corvis -> ef).
/// \param matches: The correspondence set.
void FindCorrespondence(const std::unordered_map<int, Model> &tgt,
                        const std::unordered_map<int, Model> &src,
                        const Eigen::Matrix<double, 4, 4> &T_tgt_src,
                        three::CorrespondenceSet &matches,
                        double threshold = 0.2);

/// \brief: Naive implementation of SE3 smoothing: Average on tangent space. Might not be optimal.
/// Then need to seek for iterative method for numeric optimization.
Eigen::Matrix4d OptimizeAlignment(
    const std::unordered_map<int, Model> &tgt,
    const std::unordered_map<int, Model> &src,
    const three::CorrespondenceSet &matches);

/// \brief: ICP to refine the alignment.
three::RegistrationResult ICPRefinement(std::shared_ptr<three::PointCloud> scene,
                                        const std::unordered_map<int, Model> &src,
                                        const Eigen::Matrix4d &T_scene_src,
                                        const folly::dynamic &options);


/// \brief: Assemble the scene mesh by:
/// 1) Retrieving proper meshes from CAD database;
/// 2) Apply the pose estimate;
/// 3) Apply the Corvis to Ground Truth (ElasticFusion) alignment.
/// \param objects: List of shape name, pose pairs.
/// \param alignment: Corvis to EF alignment.
/// \param vertices, faces: Assembled mesh.
void AssembleScene(const folly::dynamic &config,
                   const std::list<std::pair<std::string, Eigen::Matrix<double, 3, 4> > > &objects,
                   const Eigen::Matrix<double, 3, 4> &alignment,
                   std::vector<Eigen::Matrix<double, 6, 1>> &vertices,
                   std::vector<Eigen::Matrix<int, 3, 1>> &faces);
/// \brief: Visualize semantic reconstruction with sparse point cloud from VIO.
void AssembleResult(const folly::dynamic &config,
                     Eigen::Matrix<double, Eigen::Dynamic, 6> *V=nullptr,
                     Eigen::Matrix<int, Eigen::Dynamic, 3> *F=nullptr);
void VisualizeResult(const folly::dynamic &config);
/// \brief: Visualize ground truth with annotation.
void AssembleGroundTruth(const folly::dynamic &config,
                          Eigen::Matrix<double, Eigen::Dynamic, 6> *V=nullptr,
                          Eigen::Matrix<int, Eigen::Dynamic, 3> *F=nullptr);
/// \brief: Entrance to quantitative evaluation of surface reconstruction.
/// MeasureSurfaceError is called internally to compute error measure.
void QuantitativeEvaluation(folly::dynamic config);




}

