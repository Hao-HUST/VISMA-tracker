//
// Created by visionlab on 2/20/18.
//
#include "tool.h"

// 3rd parth
#include "IO/IO.h"
#include "Visualization/Visualization.h"
#include "igl/readOBJ.h"
#include "sophus/se3.hpp"
#include "folly/FileUtil.h"
#include "folly/json.h"

// feh
#include "constrained_icp.h"
#include "geometry.h"
#include "io_utils.h"

namespace feh {



void FindCorrespondence(const std::unordered_map<int, Model> &tgt,
                        const std::unordered_map<int, Model> &src,
                        const Eigen::Matrix<double, 4, 4> &T_tgt_src,
                        three::CorrespondenceSet &matches,
                        double threshold) {
    for (const auto &kv1 : src) {
        const Model &m1 = kv1.second;
        double min_dist = threshold;
        int best_match = -1;

        for (const auto &kv2 : tgt) {
            const Model &m2 = kv2.second;
            auto T_ef_model = T_tgt_src * m1.model_to_scene_;   // model -> ef
            // m2.model_to_scene_: model -> ef
            auto dT = T_ef_model.inverse() * m2.model_to_scene_;    // should close to identity
            if (dT.block<3, 1>(0, 3).norm() < min_dist) {
                min_dist = dT.block<3, 1>(0, 3).norm();
                best_match = kv2.first;
            }
        }
        if (best_match >= 0) {
            matches.push_back({kv1.first, best_match});
        }
    }
}

Eigen::Matrix4d OptimizeAlignment(
    const std::unordered_map<int, Model> &tgt,
    const std::unordered_map<int, Model> &src,
    const three::CorrespondenceSet &matches) {
    std::vector<double> w(matches.size(), 1.0 / matches.size());
    Eigen::Matrix<float, 6, 1> sum, last_sum;
    int iter = 0;
    for (; iter < 100; ++iter) {
        sum.setZero();
        for (int k = 0; k < matches.size(); ++k) {
            const auto &match = matches[k];
            auto dT = tgt.at(match[1]).model_to_scene_ * src.at(match[0]).model_to_scene_.inverse();
            Eigen::Matrix<float, 6, 1> tangent = Sophus::SE3f(dT.cast<float>()).log();
            sum += w[k] * tangent;
        }
        auto T = Sophus::SE3d::exp(sum.cast<double>());
        // compute weights
        double sum_w(0);
        for (int k = 0; k < matches.size(); ++k) {
            const auto &match = matches[k];
            auto dT = tgt.at(match[1]).model_to_scene_ * (T.matrix() * src.at(match[0]).model_to_scene_).inverse();
            w[k] = 1.0 / std::max<double>(1e-4, Sophus::SE3f(dT.cast<float>()).log().norm());
            sum_w += w[k];
        }
        for (auto &each_w : w) each_w /= sum_w;
        if ((last_sum - sum).norm() / sum.norm() < 1e-5) break;
        last_sum = sum;
    }
    std::cout << "Alignment optimization finished after " << iter << " iterations\n";
    return Sophus::SE3d::exp(sum.cast<double>()).matrix();
}

three::RegistrationResult RegisterScenes(
    const std::unordered_map<int, Model> &tgt,
    const std::unordered_map<int, Model> &src) {
    three::CorrespondenceSet best_matches;
    Eigen::Matrix4d best_T_tgt_src;
    best_T_tgt_src.setIdentity();

    for (const auto &kv1 : src) {
        const Model &m1 = kv1.second;
        std::cout << "model1.name=" << m1.model_name_ << "\n";
        for (const auto &kv2 : tgt) {
            const Model &m2 = kv2.second;
            std::cout << "model2.name=" << m2.model_name_ << "\n";
            if (m1.model_name_ == m2.model_name_) {
                // ONLY TEST WHEN THE TWO MODELS HAVE THE SAME SHAPE
                // SOURCE TO TARGET TRANSFORMATION
                auto T_tgt_src = m2.model_to_scene_ * m1.model_to_scene_.inverse(); // corvis -> elasticfusion (ef)
                std::cout << "T_tgt_src=\n" << T_tgt_src << "\n";
                // NOW LET'S CHECK THE RESIDUAL OF THIS PROPOSED TRANSFORMATION
                three::CorrespondenceSet matches;
                FindCorrespondence(tgt, src, T_tgt_src, matches, 0.5);
                if (matches.size() > best_matches.size()) {
                    best_matches = matches;
                    best_T_tgt_src = T_tgt_src;
                }
            }
        }
    }

    best_T_tgt_src = OptimizeAlignment(tgt, src, best_matches);
    three::RegistrationResult result(best_T_tgt_src);
    result.correspondence_set_ = best_matches;
    return result;
}

void EvaluationTool(const folly::dynamic &config) {
    // EXTRACT PATHS
    std::string database_dir = config["CAD_database_root"].getString();

    std::string dataroot = config["dataroot"].getString();
    std::string dataset = config["dataset"].getString();
    std::string scene_dir = dataroot + "/" + dataset + "/";
    std::string fragment_dir = scene_dir + "/fragments/";
    // LOAD SCENE POINT CLOUD
    auto scene = std::make_shared<three::PointCloud>();
    three::ReadPointCloudFromPLY(scene_dir + "/test.klg.ply", *scene);
    // READ GROUND TRUTH POSES
    std::string contents;
    folly::readFile(folly::sformat("{}/alignment.json", fragment_dir).c_str(), contents);
    folly::dynamic gt_json = folly::parseJson(folly::json::stripComments(contents));

    // CONSTRUCT GROUND TRUTH UNORDERED_MAP
    std::unordered_map<int, Model> models;
    int counter(0);
    for (auto &key_obj : gt_json.keys()) {
        std::string key = key_obj.asString();
        auto &this_model = models[counter];

        this_model.model_to_scene_.block<3, 4>(0, 0) = io::GetMatrixFromDynamic<double, 3, 4>(gt_json, key);
        this_model.model_name_ = key.substr(0, key.find_last_of('_'));
        igl::readOBJ(folly::sformat("{}/{}.obj", database_dir, this_model.model_name_),
                     this_model.V_,
                     this_model.F_);

        std::shared_ptr <three::PointCloud> model_pc = std::make_shared<three::PointCloud>();
        model_pc->points_ = SamplePointCloudFromMesh(
            this_model.V_, this_model.F_, config["visualization"]["model_samples"].asInt());
        model_pc->colors_.resize(model_pc->points_.size(), {0, 255, 0});
        model_pc->Transform(this_model.model_to_scene_);    // TRANSFORM TO EF (ELASTICFUSION) FRAME
        this_model.pcd_ptr_ = model_pc;

        std::cout << "key=" << key << "\n" << models[counter].model_to_scene_ << "\n";
        ++counter;
    }

    // PUT OBJECTS IN THE SCENE ACCORDING TO GROUND TRUTH POSE
    if (config["evaluation"]["show_annotation"].asBool()) {
        for (const auto &key_val : models) {
            auto model = key_val.second;
            *scene += *(model.pcd_ptr_);
        }
        three::DrawGeometries({scene}, "Ground truth overlay");
    }

    // LOAD RESULT FILE
    std::string result_file = folly::sformat("{}/result.json", scene_dir);
    std::cout << "result file=" << result_file << "\n";
    folly::readFile(result_file.c_str(), contents);
    folly::dynamic result = folly::parseJson(folly::json::stripComments(contents));
    // ITERATE AND GET THE LAST ONE
    auto packet = result.at(result.size() - 1);
    auto scene_est = std::make_shared<three::PointCloud>();
    std::unordered_map<int, Model> models_est;
    for (const auto &obj : packet) {
        auto pose = io::GetMatrixFromDynamic<double, 3, 4>(obj, "model_pose");
        std::cout << folly::format("id={}\nstatus={}\nshape={}\npose=\n",
                                   obj["id"].asInt(),
                                   obj["status"].asInt(),
                                   obj["model_name"].asString())
                  << pose << "\n";

        auto &this_model = models_est[obj["id"].asInt()];
        this_model.model_name_ = obj["model_name"].asString();
        this_model.model_to_scene_.block<3, 4>(0, 0) = pose;
        igl::readOBJ(folly::sformat("{}/{}.obj",
                                    database_dir,
                                    this_model.model_name_),
                     this_model.V_, this_model.F_);

        std::shared_ptr <three::PointCloud> model_pc = std::make_shared<three::PointCloud>();
        model_pc->points_ = SamplePointCloudFromMesh(
            this_model.V_, this_model.F_,
            config["visualization"]["model_samples"].asInt());
        model_pc->colors_.resize(model_pc->points_.size(), {255, 0, 0});
        model_pc->Transform(this_model.model_to_scene_);    // ALREADY IN CORVIS FRAME
        this_model.pcd_ptr_ = model_pc;

        *scene_est += *model_pc;
    }

    three::DrawGeometries({scene_est}, "reconstructed scene");
    auto ret = RegisterScenes(models, models_est);
    auto T_ef_corvis = ret.transformation_;
    std::cout << "T_ef_corvis=\n" << T_ef_corvis << "\n";
    for (int i = 0; i < ret.correspondence_set_.size(); ++i) {
        std::cout << folly::format("{}-{}\n", ret.correspondence_set_[i][0], ret.correspondence_set_[i][1]);
    }

    if (config["evaluation"]["ICP_refinement"].asBool()) {
        // RE-LOAD THE SCENE
        std::shared_ptr<three::PointCloud> raw_scene = std::make_shared<three::PointCloud>();
        three::ReadPointCloudFromPLY(scene_dir + "/test.klg.ply", *raw_scene);
        // FIXME: MIGHT NEED CROP THE 3D REGION-OF-INTEREST HERE
        auto result = ICPRefinement(raw_scene,
                                    models_est,
                                    T_ef_corvis,
                                    config["evaluation"]);
        T_ef_corvis = result.transformation_;
    }
    // save the alignment
    folly::dynamic out = folly::dynamic::object();
    io::WriteMatrixToDynamic(out, "T_ef_corvis", T_ef_corvis.block<3, 4>(0, 0));
    std::string output_path = scene_dir + "/result_alignment.json";
    folly::writeFile(folly::toPrettyJson(out), output_path.c_str());
    std::cout << "T_ef_corvis written to " << output_path << "\n";

//    three::ReadPointCloudFromPLY(config["scene_directory"].getString() + "/test.klg.ply", *scene);
    // NOW LETS LOOK AT THE ESTIMATED SCENE IN RGB-D SCENE FRAME
    for (const auto &kv : models_est) {
        const auto &this_model = kv.second;
        this_model.pcd_ptr_->Transform(T_ef_corvis);
        *scene += *(this_model.pcd_ptr_);
    }
    three::DrawGeometries({scene});
    three::WritePointCloud(fragment_dir+"/augmented_view.ply", *scene);
}


three::RegistrationResult ICPRefinement(std::shared_ptr<three::PointCloud> scene,
                                        const std::unordered_map<int, Model> &src,
                                        const Eigen::Matrix4d &T_scene_src,
                                        const folly::dynamic &options) {
    // CONSTRUCT ESTIMATED SCENE
    auto scene_est = std::make_shared<three::PointCloud>();
    for (const auto &kv : src) {
        const auto &this_model = kv.second;
        auto model_ptr = std::make_shared<three::PointCloud>();
        model_ptr->points_ = SamplePointCloudFromMesh(this_model.V_, this_model.F_, options["samples_per_model"].asInt());
        model_ptr->Transform(this_model.model_to_scene_);
        *scene_est += *model_ptr;
    }

    scene = three::VoxelDownSample(*scene, options.getDefault("voxel_size", 0.02).asDouble());
    three::RegistrationResult result;
    if (options["use_point_to_plane"].asBool()) {
        result = three::RegistrationICP(*scene_est,
                                        *scene,
                                        options.getDefault("max_distance", 0.05).asDouble(),
                                        T_scene_src,
                                        three::TransformationEstimationPointToPlane());
    } else {
        result = three::RegistrationICP(*scene_est,
                                        *scene,
                                        options.getDefault("max_distance", 0.05).asDouble(),
                                        T_scene_src);
    }
    std::cout << folly::format("fitness={}; inlier_rmse={}\n", result.fitness_, result.inlier_rmse_);
    return result;
}

void QuantitativeEvaluation(folly::dynamic config) {
    // disable original mesh
    CHECK(!config["scene_visualization"]["show_original_scene"].getBool());

    // assemble result scene mesh
    Eigen::Matrix<double, Eigen::Dynamic, 6> tmp;

    Eigen::Matrix<double, Eigen::Dynamic, 3> Vr;
    Eigen::Matrix<int, Eigen::Dynamic, 3> Fr;
    AssembleResult(config, &tmp, &Fr);
    Vr = tmp.leftCols(3);
    std::cout << TermColor::cyan << "Result scene mesh assembled" << TermColor::endl;

    // assemble ground truth scene mesh
    Eigen::Matrix<double, Eigen::Dynamic, 3> Vg;
    Eigen::Matrix<int, Eigen::Dynamic, 3> Fg;
    AssembleGroundTruth(config, &tmp, &Fg);
    Vg = tmp.leftCols(3);
    std::cout << TermColor::cyan << "Ground truth scene mesh assembled" << TermColor::endl;

    // measure surface error
    std::cout << TermColor::cyan << "Computing error measure ..." << TermColor::endl;
    auto stats = MeasureSurfaceError(Vr, Fr, Vg, Fg,
                                     folly::dynamic::object("num_samples", std::min<uint64_t>(500000, Fg.rows()*100)));
    std::cout << "mean=" << stats.mean_ << "\n";
    std::cout << "std=" << stats.std_ << "\n";
    std::cout << "min=" << stats.min_ << "\n";
    std::cout << "max=" << stats.max_ << "\n";
    std::cout << "median=" << stats.median_ << "\n";

    // write out result
    std::string quant_file = folly::sformat("{}/{}/surface_error.json", config["dataroot"].getString(), config["dataset"].getString());
    folly::dynamic out_json = folly::dynamic::object
        ("mean", stats.mean_)
        ("std", stats.std_)
        ("min", stats.min_)
        ("max", stats.max_)
        ("median", stats.median_);

    folly::writeFile(folly::toPrettyJson(out_json), quant_file.c_str());
}

}

