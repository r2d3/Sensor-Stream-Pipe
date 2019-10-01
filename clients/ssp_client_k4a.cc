//
// Created by amourao on 26-06-2019.
//

#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>

#include <k4a/k4a.h>
#include <opencv2/imgproc.hpp>
#include <zmq.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include "../utils/logger.h"

#include "../readers/network_reader.h"
#include "../utils/kinect_utils.h"
#include "../utils/video_utils.h"

void print_body_information(k4abt_body_t body) {
  std::cout << "Body ID: " << body.id << std::endl;
  for (int i = 0; i < (int)K4ABT_JOINT_COUNT; i++) {
    k4a_float3_t position = body.skeleton.joints[i].position;
    k4a_quaternion_t orientation = body.skeleton.joints[i].orientation;
    printf("Joint[%d]: Position[mm] ( %f, %f, %f ); Orientation ( %f, %f, %f, "
           "%f) \n",
           i, position.v[0], position.v[1], position.v[2], orientation.v[0],
           orientation.v[1], orientation.v[2], orientation.v[3]);
  }
}

void print_body_index_map_middle_line(k4a::image body_index_map) {
  uint8_t *body_index_map_buffer = body_index_map.get_buffer();

  // Given body_index_map pixel type should be uint8, the stride_byte should be
  // the same as width
  // TODO: Since there is no API to query the byte-per-pixel information, we
  // have to compare the width and stride to know the information. We should
  // replace this assert with proper byte-per-pixel query once the API is
  // provided by K4A SDK.
  assert(body_index_map.get_stride_bytes() ==
         body_index_map.get_width_pixels());

  int middle_line_num = body_index_map.get_height_pixels() / 2;
  body_index_map_buffer = body_index_map_buffer +
                          middle_line_num * body_index_map.get_width_pixels();

  std::cout << "BodyIndexMap at Line " << middle_line_num << ":" << std::endl;
  for (int i = 0; i < body_index_map.get_width_pixels(); i++) {
    std::cout << (int)*body_index_map_buffer << ", ";
    body_index_map_buffer++;
  }
  std::cout << std::endl;
}

int main(int argc, char *argv[]) {

  spdlog::set_level(spdlog::level::debug);
  av_log_set_level(AV_LOG_QUIET);

  srand(time(NULL) * getpid());

  try {

    if (argc < 2) {
      std::cerr << "Usage: ssp_client <port> (<log level>) (<log file>)"
                << std::endl;
      return 1;
    }
    std::string log_level = "debug";
    std::string log_file = "";

    if (argc > 2)
      log_level = argv[2];
    if (argc > 3)
      log_file = argv[3];

    int port = std::stoi(argv[1]);
    NetworkReader reader(port);

    reader.init();

    k4a::calibration sensor_calibration;
    bool calibration_set = false;
    k4abt::tracker tracker;

    std::unordered_map<std::string, IDecoder *> decoders;

    while (reader.HasNextFrame()) {
      reader.NextFrame();
      std::vector<FrameStruct> f_list = reader.GetCurrentFrame();
      for (FrameStruct f : f_list) {
        std::string decoder_id = f.stream_id + std::to_string(f.sensor_id);

        if (f.camera_calibration_data.type == 0 && calibration_set == false) {

          const k4a_depth_mode_t d = static_cast<const k4a_depth_mode_t>(
              f.camera_calibration_data.extra_data[0]);
          const k4a_color_resolution_t r =
              static_cast<const k4a_color_resolution_t>(
                  f.camera_calibration_data.extra_data[1]);

          sensor_calibration = k4a::calibration::get_from_raw(
              reinterpret_cast<char *>(&f.camera_calibration_data.data[0]),
              f.camera_calibration_data.data.size(), d, r);

          calibration_set = true;

          tracker = k4abt::tracker::create(sensor_calibration);
        }
      }

      k4a::capture sensor_capture = k4a::capture::create();
      FrameStructToK4A(f_list, sensor_capture, decoders);

      if (!tracker.enqueue_capture(sensor_capture)) {
        // It should never hit timeout when K4A_WAIT_INFINITE is set.
        std::cout << "Error! Add capture to tracker process queue timeout!"
                  << std::endl;
        break;
      }

      k4abt::frame body_frame = tracker.pop_result();
      if (body_frame != nullptr) {
        size_t num_bodies = body_frame.get_num_bodies();
        std::cout << num_bodies << " bodies are detected!" << std::endl;

        for (size_t i = 0; i < num_bodies; i++) {
          k4abt_body_t body = body_frame.get_body(i);
          print_body_information(body);
        }

        k4a::image body_index_map = body_frame.get_body_index_map();
        if (body_index_map != nullptr) {
          print_body_index_map_middle_line(body_index_map);
        } else {
          std::cout << "Error: Failed to generate bodyindex map!" << std::endl;
        }
      } else {
        //  It should never hit timeout when K4A_WAIT_INFINITE is set.
        std::cout << "Error! Pop body frame result time out!" << std::endl;
        break;
      }
    }

  } catch (std::exception &e) {
    spdlog::error(e.what());
  }

  return 0;
}
