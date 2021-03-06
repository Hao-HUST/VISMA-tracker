//
// Created by visionlab on 3/18/18.
//
#include "viewer.h"
// Frame Inspector
#include "dataloaders.h"
#include "tracker_utils.h"
#include "renderer.h"

// 3rd party
#include "opencv2/opencv.hpp"
#include "folly/FileUtil.h"
#include "folly/json.h"
#include "tbb/parallel_for.h"

void DrawOneFrame(const cv::Mat &img,
                  const cv::Mat &edgemap,
                  const vlslam_pb::BoundingBoxList &bboxlist,
                  const Sophus::SE3f &gwc,
                  const Sophus::SO3f &Rg,
                  feh::RendererPtr render_engine,
                  const folly::dynamic &config,
                  const folly::dynamic &result, // result at this time stamp
                  cv::Mat *ptr_input_with_proposals,
                  cv::Mat *ptr_edgemap,
                  cv::Mat *ptr_input_with_contour)
{
    std::string dataset_path = folly::sformat("{}{}", config["experiment_root"].getString(), config["dataset"].getString());
    std::string database_dir = config["CAD_database_root"].getString();
    std::string scene_dir = config["dataroot"].getString() + "/" + config["dataset"].getString();

    // OVERLAY BOUNDING BOX PROPOSALS ON INPUT IMAGE
    if (ptr_input_with_proposals) {
        cv::Mat &input_with_proposals(*ptr_input_with_proposals);
        input_with_proposals = img.clone();
        for (auto bbox : bboxlist.bounding_boxes()) {
            if (bbox.class_name() == "chair" && bbox.scores(0) > 0.8) {
                auto c = feh::tracker::kColorMap.at(bbox.class_name());
                cv::rectangle(input_with_proposals,
                              cv::Point((int)(bbox.top_left_x()), (int)(bbox.top_left_y())),
                              cv::Point((int)(bbox.bottom_right_x()), (int)(bbox.bottom_right_y())),
                              cv::Scalar(c[0], c[1], c[2]), 2);
            }
        }
    }

    // INVERT EDGEMAP FOR PLEASING VISUALIZATION
    if (ptr_edgemap) {
        cv::Mat &tmp(*ptr_edgemap);
        tmp = 255 - edgemap;
        cv::cvtColor(tmp, tmp, CV_GRAY2RGB);
    }

    // Z-BUFFER COLOR-ENCODED BY INSTANCE LABEL
//    folly::readFile((scene_dir + "/result.json").c_str(), contents);
//    // FIXME: RESULT FILE SHOULD KEEP TRACK OF TIMESTAMP
//    folly::dynamic result = folly::parseJson(folly::json::stripComments(contents)).at(index); //-130);
    // OVERWRITE SOME PARAMETERS

    if (ptr_input_with_contour) {
        cv::Size size(render_engine->cols(), render_engine->rows());
        cv::Mat zbuf(size, CV_32FC1);
        zbuf.setTo(0);
        cv::Mat segmask(size, CV_32SC1);
        segmask.setTo(-1);
        auto cm = feh::GenerateRandomColorMap<8>();
        cm[0] = {255, 255, 255};    // white background
        cv::Mat input_with_contour = img.clone();

        for (const auto &obj : result) {
            auto pose = feh::GetMatrixFromJson<float, 3, 4>(obj, "model_pose");
//            std::cout << folly::format("id={}\nstatus={}\nshape={}\npose=\n",
//                                       obj["id"].asInt(),
//                                       obj["status"].asInt(),
//                                       obj["model_name"].asString())
//                      << pose << "\n";

            Sophus::SE3f gwm(pose.block<3,3>(0,0), pose.block<3,1>(0, 3));
            Sophus::SE3f gcm = gwc.inverse() * gwm;

            int instance_id = obj["id"].asInt();
            std::string model_name = obj["model_name"].asString();
            std::vector<float> v;
            std::vector<int> f;
            feh::LoadMeshFromObjFile(
                folly::sformat("{}/{}.obj", database_dir, model_name),
                v, f);
            render_engine->SetMesh(v, f);
            cv::Mat depth(size, CV_32FC1);
            depth.setTo(0);
            render_engine->RenderDepth(gcm.matrix(), depth);
            feh::tracker::PrettyDepth(depth);

            cv::Mat contour(size, CV_8UC1);
            render_engine->RenderEdge(gcm.matrix(), contour);
            cv::dilate(contour, contour, cv::Mat());
            feh::tracker::OverlayMaskOnImage(contour, input_with_contour, false, &cm[instance_id+1][0]);
//        cv::imshow(folly::sformat("depth#{}", instance_id), depth);
//        cv::Mat mask(render_engine->rows(), render_engine->cols(), CV_8UC1);
//        render_engine->RenderMask(gcm.matrix(), mask);
//        cv::imshow(folly::sformat("mask#{}", instance_id), mask);

            auto op = [instance_id, &depth, &segmask, &zbuf]
                (const tbb::blocked_range<int> &range) {
                for (int i = range.begin(); i < range.end(); ++i) {
                    for (int j = 0; j < depth.cols; ++j) {
                        float val(depth.at<float>(i, j));
                        if (val > 0) {
                            // only on foreground
                            float zbuf_val(zbuf.at<float>(i, j));
                            if (zbuf_val == 0 || val < zbuf_val) {
                                zbuf.at<float>(i, j) = val;
                                segmask.at<int32_t>(i, j) = instance_id;
                            }
                        }
                    }
                }};
            tbb::parallel_for(tbb::blocked_range<int>(0, depth.rows), op);
        }

        // GENERATE COLOR-ENCODED DEPTH MAP
        cv::Mat zbuf_viz = feh::tracker::PrettyZBuffer(zbuf);
        auto segmask_viz = feh::tracker::PrettyLabelMap(segmask, cm);
        auto shade_op = [&zbuf_viz, &segmask_viz]
            (const tbb::blocked_range<int> &range) {
            for (int i = range.begin(); i < range.end(); ++i) {
                for (int j = 0; j < zbuf_viz.cols; ++j) {
                    if (zbuf_viz.at<uint8_t>(i, j) > 0)
                        segmask_viz.at<cv::Vec3b>(i, j) *= zbuf_viz.at<uint8_t>(i, j) / 255.0;
                }
            }
        };
        tbb::parallel_for(tbb::blocked_range<int>(0, zbuf_viz.rows), shade_op);
        for (int i = 0; i < segmask_viz.rows; ++i) {
            for (int j = 0; j < segmask_viz.cols; ++j) {
                auto color = segmask_viz.at<cv::Vec3b>(i, j);
                if (!(color[0] == 255 && color[1] == 255 && color[2] == 255)) {
                    input_with_contour.at<cv::Vec3b>(i, j) = color;
                }
            }
        }
        *ptr_input_with_contour = input_with_contour.clone();
    }
}

