// stl
#include <fstream>
#include <iostream>

// 3rd party
#include "glog/logging.h"
#include "json/json.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "zmqpp/zmqpp.hpp"

// feh
#include "tracker_utils.h"
#include "message_utils.h"
#include "dataloaders.h"
#include "gravity_aligned_tracker.h"
#include "vlslam.pb.h"
#include "initializer.h"

using namespace feh;


int main(int argc, char **argv) {
    std::string config_file("../cfg/DFTracker.json");
    if (argc > 1) {
        config_file = argv[1];
    }

    auto config = LoadJson(config_file);
    auto cam_cfg = LoadJson(config["camera_config"].asString());

    // setup zmq client
    zmqpp::context context;
    std::shared_ptr<zmqpp::socket> socket = nullptr;
    if (config["request_detection"].asBool()) {
      socket = std::make_shared<zmqpp::socket>(context, zmqpp::socket_type::request);
      socket->connect(absl::StrFormat("tcp://localhost:%d", config["port"].asInt()));
    } 

    MatXf V;
    MatXi F;
    LoadMesh(config["CAD_model"].asString(), V, F);
    tracker::NormalizeVertices(V);
    tracker::RotateVertices(V, -M_PI / 2);
    tracker::FlipVertices(V);

    auto control_pts = tracker::GenerateControlPoints(V);
    Mat3 K;
    K << cam_cfg["fx"].asFloat(), 0, cam_cfg["cx"].asFloat(),
         0, cam_cfg["fy"].asFloat(), cam_cfg["cy"].asFloat(),
         0, 0, 1;

    std::string dataset_path(config["dataset_root"].asString() + config["dataset"].asString());

    int wait_time(0);
    wait_time = config["wait_time"].asInt();

    VlslamDatasetLoader loader(dataset_path);

    cv::namedWindow("tracker view", CV_WINDOW_NORMAL);
    cv::namedWindow("DF", CV_WINDOW_NORMAL);
    cv::namedWindow("Detection", CV_WINDOW_NORMAL);
    cv::Mat disp_tracker, disp_DF, disp_det;

    SE3 camera_pose_t0;

    // initialization in camera frame
    Mat3 Rinit = Mat3::Identity();
    Vec3 Tinit = Vec3::Zero();
    Tinit = GetVectorFromJson<ftype, 3>(config, "Tinit");
    SE3 g_init(Rinit, Tinit);

    std::shared_ptr<GravityAlignedTracker> tracker{nullptr};

    Timer timer;
    for (int i = 0; i < loader.size(); ++i) {
        cv::Mat img, edgemap;
        vlslam_pb::BoundingBoxList bboxlist;
        SE3 gwc;
        SO3 Rg;

        std::string imagepath;
        bool success = loader.Grab(i, img, edgemap, bboxlist, gwc, Rg, imagepath);
        if (!success) break;

        if (socket) {
          zmqpp::message msg;
          msg.add_raw<uint8_t>(img.data, img.rows * img.cols * 3);
          socket->send(msg);
          // receive message
          std::string bbox_msg;
          bool recv_ok = socket->receive(bbox_msg);
          if (recv_ok) {
            vlslam_pb::NewBoxList boxlist;
            boxlist.ParseFromString(bbox_msg);
            disp_det = DrawBoxList(img, boxlist);

            for (auto box : boxlist.boxes()) {
              g_init = Initialize(control_pts, 
                  KeypointsFromBox(box, cam_cfg["rows"].asInt(), cam_cfg["cols"].asInt()),
                  K);
              // apply correction
              Mat3 correction;
              correction << -1, 0, 0,
                         0, -1, 0,
                         0, 0, 1;
              g_init = SE3::from_matrix3x4(correction * g_init.matrix3x4());
              // FIXME: project to rotation around gravity
              // Ideally, object pose estimation should be parametrized in gravity aligned frame.
              // Eigen::AngleAxisf aa(g_init.so3().matrix());
              std::cout << "g_init\n" << g_init.matrix3x4() << std::endl;
            }

            // initializer.solve(control_pts, newboxlist);
          } else std::cout << TermColor::red << "failed to receive message" << TermColor::endl;

          absl::SleepFor(absl::Milliseconds(10));
          cv::imshow("Detection", disp_det);
        }

        // std::cout << "gwc=\n" << gwc.matrix3x4() << std::endl;
        // std::cout << "Rg=\n" << Rg.matrix() << std::endl;

        if (tracker == nullptr) {
            tracker = std::make_shared<GravityAlignedTracker>(
                img, edgemap,
                Vec2i{cam_cfg["rows"].asInt(), cam_cfg["cols"].asInt()},
                cam_cfg["fx"].asFloat(), cam_cfg["fy"].asFloat(),
                cam_cfg["cx"].asFloat(), cam_cfg["cy"].asFloat(),
                g_init,
                V, F);
            tracker->UpdateCameraPose(gwc);
            tracker->UpdateGravity(Rg);
        } else {
            tracker->UpdateImage(img, edgemap);
            tracker->UpdateCameraPose(gwc);
            tracker->UpdateGravity(Rg);
        }


        timer.Tick("tracking");
        float cost = tracker->Minimize(config["iterations"].asInt());
        float duration = timer.Tock("tracking");
        std::cout << timer;
        // std::cout << "cost=" << cost << std::endl;
        disp_tracker = tracker->RenderEdgepixels();
        cv::putText(disp_tracker, 
                absl::StrFormat("%0.2f FPS", 1000 / duration),
                cv::Point(20, 20), CV_FONT_HERSHEY_PLAIN, 2, cv::Scalar(0, 255, 0), 2);
        cv::imshow("tracker view", disp_tracker);

        disp_DF = tracker->GetDistanceField();
        cv::imshow("DF", disp_DF);


        if (config["save"].asBool()) {
            cv::imwrite(absl::StrFormat("%04d_projection.jpg", i), disp_tracker);
            cv::imwrite(absl::StrFormat("%04d_DF.jpg", i), disp_DF);
        }
        char ckey = cv::waitKey(wait_time);
        if (ckey == 'q') break;

//        // FIXME: CAN ONLY HANDLE CHAIR
//        for (int j = 0; j < bboxlist.bounding_boxes_size(); ) {
//            if (bboxlist.bounding_boxes(j).class_name() != "chair"
//                || bboxlist.bounding_boxes(j).scores(0) < 0.8) {
//                bboxlist.mutable_bounding_boxes()->DeleteSubrange(j, 1);
//            } else ++j;
//        }
    }

}
