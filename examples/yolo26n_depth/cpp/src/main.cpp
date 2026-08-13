#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <rknn_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {
constexpr int kModelSize = 768;

struct Letterbox {
  float scale{};
  int pad_x{}, pad_y{}, resized_w{}, resized_h{};
};

using Clock = std::chrono::steady_clock;

double milliseconds(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

void check(int code, const char* what) {
  if (code != RKNN_SUCC)
    throw std::runtime_error(std::string(what) + " failed: " +
                             std::to_string(code));
}

std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) throw std::runtime_error("cannot open model: " + path);
  const auto n = f.tellg();
  if (n <= 0) throw std::runtime_error("empty model: " + path);
  std::vector<uint8_t> data(static_cast<size_t>(n));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(data.data()), n);
  if (!f) throw std::runtime_error("cannot read model: " + path);
  return data;
}

GstElement* parse_pipeline(const std::string& description) {
  GError* error = nullptr;
  GstElement* pipeline = gst_parse_launch(description.c_str(), &error);
  if (!pipeline) {
    const std::string message = error ? error->message : "unknown error";
    if (error) g_error_free(error);
    throw std::runtime_error("GStreamer pipeline error: " + message + "\n" +
                             description);
  }
  if (error) g_error_free(error);
  return pipeline;
}

struct RtspState {
  std::mutex mutex;
  GstElement* source = nullptr;
};

void media_unprepared(GstRTSPMedia*, gpointer user_data) {
  auto* state = static_cast<RtspState*>(user_data);
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->source) gst_object_unref(state->source);
  state->source = nullptr;
  std::cout << "RTSP client disconnected" << std::endl;
}

void media_configure(GstRTSPMediaFactory*, GstRTSPMedia* media,
                     gpointer user_data) {
  auto* state = static_cast<RtspState*>(user_data);
  GstElement* pipeline = gst_rtsp_media_get_element(media);
  GstElement* source =
      gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "processed");
  gst_object_unref(pipeline);
  if (!source) {
    std::cerr << "error: RTSP media has no processed appsrc" << std::endl;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->source) gst_object_unref(state->source);
    state->source = source;
  }
  g_signal_connect(media, "unprepared", G_CALLBACK(media_unprepared), state);
  std::cout << "RTSP client connected" << std::endl;
}

GstRTSPServer* start_rtsp_server(const std::string& service,
                                const std::string& mount,
                                const std::string& factory_pipeline,
                                RtspState* state) {
  GstRTSPServer* server = gst_rtsp_server_new();
  gst_rtsp_server_set_address(server, "0.0.0.0");
  gst_rtsp_server_set_service(server, service.c_str());
  GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server);
  GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
  gst_rtsp_media_factory_set_launch(factory, factory_pipeline.c_str());
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  gst_rtsp_media_factory_set_latency(factory, 0);
  g_signal_connect(factory, "media-configure", G_CALLBACK(media_configure),
                   state);
  gst_rtsp_mount_points_add_factory(mounts, mount.c_str(), factory);
  gst_object_unref(mounts);
  if (gst_rtsp_server_attach(server, nullptr) == 0) {
    gst_object_unref(server);
    throw std::runtime_error("cannot listen on RTSP port " + service +
                             " (already in use or unavailable)");
  }
  return server;
}

void require_element(const char* name) {
  GstElementFactory* factory = gst_element_factory_find(name);
  if (!factory)
    throw std::runtime_error(std::string("required GStreamer element is missing: ") +
                             name);
  gst_object_unref(factory);
}

void throw_on_bus_error(GstElement* pipeline, const char* pipeline_name) {
  GstBus* bus = gst_element_get_bus(pipeline);
  GstMessage* message = gst_bus_pop_filtered(
      bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
  gst_object_unref(bus);
  if (!message) return;
  std::string detail;
  if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
    GError* error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_error(message, &error, &debug);
    detail = error ? error->message : "unknown GStreamer error";
    if (debug) detail += std::string("; debug: ") + debug;
    if (error) g_error_free(error);
    g_free(debug);
  } else {
    detail = "unexpected end of stream";
  }
  gst_message_unref(message);
  throw std::runtime_error(std::string(pipeline_name) + " pipeline: " + detail);
}

void start_pipeline(GstElement* pipeline, const char* pipeline_name) {
  if (gst_element_set_state(pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE)
    throw std::runtime_error(std::string(pipeline_name) +
                             " pipeline failed to enter PLAYING");
  GstState state = GST_STATE_NULL;
  const GstStateChangeReturn status =
      gst_element_get_state(pipeline, &state, nullptr, 10 * GST_SECOND);
  if (status == GST_STATE_CHANGE_FAILURE) {
    throw_on_bus_error(pipeline, pipeline_name);
    throw std::runtime_error(std::string(pipeline_name) +
                             " pipeline state transition failed");
  }
  // A live RTSP output may remain ASYNC until its first buffer. Errors are
  // still checked on every frame below.
}

Letterbox letterbox_rgb(const uint8_t* src, int sw, int sh,
                        std::vector<uint8_t>& dst) {
  Letterbox lb;
  lb.scale = std::min(float(kModelSize) / sw, float(kModelSize) / sh);
  lb.resized_w = std::max(1, int(std::round(sw * lb.scale)));
  lb.resized_h = std::max(1, int(std::round(sh * lb.scale)));
  lb.pad_x = (kModelSize - lb.resized_w) / 2;
  lb.pad_y = (kModelSize - lb.resized_h) / 2;
  dst.assign(size_t(kModelSize) * kModelSize * 3, 114);
  for (int y = 0; y < lb.resized_h; ++y) {
    const float sy = (y + 0.5f) / lb.scale - 0.5f;
    const int y0 = std::clamp(int(std::floor(sy)), 0, sh - 1);
    const int y1 = std::min(y0 + 1, sh - 1);
    const float ay = std::clamp(sy - std::floor(sy), 0.0f, 1.0f);
    for (int x = 0; x < lb.resized_w; ++x) {
      const float sx = (x + 0.5f) / lb.scale - 0.5f;
      const int x0 = std::clamp(int(std::floor(sx)), 0, sw - 1);
      const int x1 = std::min(x0 + 1, sw - 1);
      const float ax = std::clamp(sx - std::floor(sx), 0.0f, 1.0f);
      auto* d = &dst[((y + lb.pad_y) * kModelSize + x + lb.pad_x) * 3];
      for (int c = 0; c < 3; ++c) {
        const float top = src[(y0 * sw + x0) * 3 + c] * (1 - ax) +
                          src[(y0 * sw + x1) * 3 + c] * ax;
        const float bot = src[(y1 * sw + x0) * 3 + c] * (1 - ax) +
                          src[(y1 * sw + x1) * 3 + c] * ax;
        d[c] = uint8_t(std::clamp(top * (1 - ay) + bot * ay, 0.0f, 255.0f));
      }
    }
  }
  return lb;
}

void turbo(float x, uint8_t* rgb) {
  // Compact polynomial approximation of Google's Turbo colour map.
  x = std::clamp(x, 0.0f, 1.0f);
  const float r = 0.13572138f + x * (4.61539260f + x * (-42.66032258f +
      x * (132.13108234f + x * (-152.94239396f + x * 59.28637943f))));
  const float g = 0.09140261f + x * (2.19418839f + x * (4.84296658f +
      x * (-14.18503333f + x * (4.27729857f + x * 2.82956604f))));
  const float b = 0.10667330f + x * (12.64194608f + x * (-60.58204836f +
      x * (110.36276771f + x * (-89.90310912f + x * 27.34824973f))));
  rgb[0] = uint8_t(std::clamp(r, 0.0f, 1.0f) * 255.0f);
  rgb[1] = uint8_t(std::clamp(g, 0.0f, 1.0f) * 255.0f);
  rgb[2] = uint8_t(std::clamp(b, 0.0f, 1.0f) * 255.0f);
}

std::pair<float, float> robust_range(const float* depth, const Letterbox& lb) {
  // Sample the valid region and use 2/98 percentiles so a few invalid pixels
  // do not flatten the visualisation.
  std::vector<float> samples;
  samples.reserve(16384);
  const int step = std::max(1, std::min(lb.resized_w, lb.resized_h) / 128);
  for (int y = lb.pad_y; y < lb.pad_y + lb.resized_h; y += step)
    for (int x = lb.pad_x; x < lb.pad_x + lb.resized_w; x += step) {
      const float v = depth[y * kModelSize + x];
      if (std::isfinite(v)) samples.push_back(v);
    }
  if (samples.empty()) return {0.0f, 1.0f};
  std::sort(samples.begin(), samples.end());
  const float lo = samples[size_t((samples.size() - 1) * 0.02)];
  float hi = samples[size_t((samples.size() - 1) * 0.98)];
  if (hi <= lo + 1e-6f) hi = lo + 1.0f;
  return {lo, hi};
}

void overlay_depth(uint8_t* frame, int fw, int fh, const float* depth,
                   const Letterbox& lb, float alpha, float& lo, float& hi) {
  std::tie(lo, hi) = robust_range(depth, lb);
  const float inv = 1.0f / (hi - lo);
  for (int y = 0; y < fh; ++y) {
    const float my = lb.pad_y + (y + 0.5f) * lb.resized_h / fh - 0.5f;
    const int y0 = std::clamp(int(std::round(my)), lb.pad_y,
                              lb.pad_y + lb.resized_h - 1);
    for (int x = 0; x < fw; ++x) {
      const float mx = lb.pad_x + (x + 0.5f) * lb.resized_w / fw - 0.5f;
      const int x0 = std::clamp(int(std::round(mx)), lb.pad_x,
                                lb.pad_x + lb.resized_w - 1);
      float normalized = (depth[y0 * kModelSize + x0] - lo) * inv;
      // Near/high response is red and far/low response is blue. This is a
      // relative per-frame visualisation; the model metadata gives no metric scale.
      uint8_t colour[3];
      turbo(normalized, colour);
      auto* p = &frame[(y * fw + x) * 3];
      for (int c = 0; c < 3; ++c)
        p[c] = uint8_t(p[c] * (1.0f - alpha) + colour[c] * alpha);
    }
  }
}

void print_tensor(const char* label, const rknn_tensor_attr& a) {
  std::cout << label << " index=" << a.index << " dims=[";
  for (uint32_t i = 0; i < a.n_dims; ++i)
    std::cout << (i ? "," : "") << a.dims[i];
  std::cout << "] fmt=" << a.fmt << " type=" << a.type << "\n";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 8) {
    std::cerr << "usage: " << argv[0]
              << " MODEL.rknn [port=8554] [mount=/recamera-depth] "
                 "[width=1280] [height=720] [fps=30] [alpha=0.65] "
                 "\n";
    return 2;
  }
  const std::string service = argc > 2 ? argv[2] : "8554";
  const std::string mount = argc > 3 ? argv[3] : "/recamera-depth";
  const int width = argc > 4 ? std::stoi(argv[4]) : 1280;
  const int height = argc > 5 ? std::stoi(argv[5]) : 720;
  const int fps = argc > 6 ? std::stoi(argv[6]) : 30;
  const float alpha = argc > 7 ? std::stof(argv[7]) : 0.65f;
  if (width <= 0 || height <= 0 || fps <= 0 || alpha < 0 || alpha > 1) {
    std::cerr << "invalid dimensions, fps, or alpha\n";
    return 2;
  }
  if (mount.empty() || mount[0] != '/') {
    std::cerr << "RTSP mount must start with /\n";
    return 2;
  }

  gst_init(&argc, &argv);
  rknn_context ctx = 0;
  GstElement *capture = nullptr, *sink = nullptr;
  GstRTSPServer* server = nullptr;
  GMainLoop* main_loop = nullptr;
  std::thread server_thread;
  RtspState rtsp_state;
  int result = 1;
  try {
    auto model = read_file(argv[1]);
    check(rknn_init(&ctx, model.data(), model.size(), 0, nullptr), "rknn_init");
    rknn_input_output_num io{};
    check(rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)), "query io");
    if (io.n_input != 1 || io.n_output != 1)
      throw std::runtime_error("depth model must have exactly 1 input and 1 output");
    rknn_tensor_attr in_attr{}, out_attr{};
    in_attr.index = 0;
    out_attr.index = 0;
    check(rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr)),
          "query input");
    check(rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attr, sizeof(out_attr)),
          "query output");
    print_tensor("input", in_attr);
    print_tensor("output", out_attr);
    if (in_attr.n_elems != size_t(kModelSize) * kModelSize * 3 ||
        out_attr.n_elems != size_t(kModelSize) * kModelSize)
      throw std::runtime_error("expected 768x768 RGB input and 768x768 depth output");

    const std::string cap =
        "v4l2src device=/dev/video13 ! "
        "video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! "
        "videoconvert ! videoscale ! video/x-raw,format=RGB,width=" +
        std::to_string(width) + ",height=" + std::to_string(height) +
        ",framerate=" + std::to_string(fps) +
        "/1 ! appsink name=capture max-buffers=1 drop=true sync=false";
    const std::string rtsp_pipeline =
        "( appsrc name=processed is-live=true do-timestamp=true format=time "
        "block=false caps=video/x-raw,format=RGB,width=" + std::to_string(width) +
        ",height=" + std::to_string(height) + ",framerate=" +
        std::to_string(fps) +
        "/1 ! queue max-size-buffers=2 leaky=downstream ! videoconvert ! " +
        "video/x-raw,format=I420 ! jpegenc quality=80 idct-method=ifast "
        "! rtpjpegpay name=pay0 pt=26 )";
    capture = parse_pipeline(cap);
    sink = gst_bin_get_by_name(GST_BIN(capture), "capture");
    if (!sink) throw std::runtime_error("capture appsink lookup failed");
    require_element("jpegenc");
    require_element("rtpjpegpay");
    server = start_rtsp_server(service, mount, rtsp_pipeline, &rtsp_state);
    main_loop = g_main_loop_new(nullptr, FALSE);
    server_thread = std::thread([main_loop] { g_main_loop_run(main_loop); });
    start_pipeline(capture, "camera capture");

    std::vector<uint8_t> frame_rgb(size_t(width) * height * 3), infer;
    uint64_t frame_id = 0;
    std::cout << "RTSP server listening on rtsp://0.0.0.0:" << service << mount
              << " (use the reCamera IP from another device)\n";
    while (true) {
      throw_on_bus_error(capture, "camera capture");
      const auto frame_begin = Clock::now();
      GstSample* sample =
          gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
      if (!sample) throw std::runtime_error("camera frame timeout");
      GstBuffer* input_buffer = gst_sample_get_buffer(sample);
      GstMapInfo map{};
      if (!gst_buffer_map(input_buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        throw std::runtime_error("frame map failed");
      }
      if (map.size != frame_rgb.size()) {
        gst_buffer_unmap(input_buffer, &map);
        gst_sample_unref(sample);
        throw std::runtime_error("unexpected RGB frame size");
      }
      std::memcpy(frame_rgb.data(), map.data, map.size);
      gst_buffer_unmap(input_buffer, &map);
      gst_sample_unref(sample);

      const auto preprocess_begin = Clock::now();
      const Letterbox lb = letterbox_rgb(frame_rgb.data(), width, height, infer);
      const auto preprocess_end = Clock::now();
      const auto run_begin = Clock::now();
      rknn_input input{};
      input.index = 0;
      input.type = RKNN_TENSOR_UINT8;
      input.fmt = RKNN_TENSOR_NHWC;
      input.size = infer.size();
      input.buf = infer.data();
      input.pass_through = 0;
      check(rknn_inputs_set(ctx, 1, &input), "rknn_inputs_set");
      check(rknn_run(ctx, nullptr), "rknn_run");
      const auto run_end = Clock::now();
      rknn_output depth_output{};
      depth_output.index = 0;
      depth_output.want_float = 1;
      check(rknn_outputs_get(ctx, 1, &depth_output, nullptr), "rknn_outputs_get");
      const auto output_end = Clock::now();
      if (depth_output.size < size_t(kModelSize) * kModelSize * sizeof(float)) {
        rknn_outputs_release(ctx, 1, &depth_output);
        throw std::runtime_error("depth output is smaller than 768x768 float32");
      }
      float lo = 0, hi = 0;
      overlay_depth(frame_rgb.data(), width, height,
                    static_cast<const float*>(depth_output.buf), lb, alpha, lo, hi);
      check(rknn_outputs_release(ctx, 1, &depth_output), "rknn_outputs_release");
      const auto postprocess_end = Clock::now();

      GstElement* current_source = nullptr;
      {
        std::lock_guard<std::mutex> lock(rtsp_state.mutex);
        if (rtsp_state.source)
          current_source = GST_ELEMENT(gst_object_ref(rtsp_state.source));
      }
      if (current_source) {
        GstBuffer* pushed =
            gst_buffer_new_allocate(nullptr, frame_rgb.size(), nullptr);
        gst_buffer_fill(pushed, 0, frame_rgb.data(), frame_rgb.size());
        const GstFlowReturn flow =
            gst_app_src_push_buffer(GST_APP_SRC(current_source), pushed);
        gst_object_unref(current_source);
        if (flow != GST_FLOW_OK && flow != GST_FLOW_FLUSHING)
          throw std::runtime_error("RTSP appsrc stopped: " +
                                   std::to_string(flow));
      }
      const auto frame_end = Clock::now();

      std::cout << std::fixed << std::setprecision(2)
                << "frame=" << frame_id
                << " preprocess_ms="
                << milliseconds(preprocess_begin, preprocess_end)
                << " npu_run_ms=" << milliseconds(run_begin, run_end)
                << " output_get_ms=" << milliseconds(run_end, output_end)
                << " inference_total_ms="
                << milliseconds(preprocess_begin, output_end)
                << " postprocess_ms="
                << milliseconds(output_end, postprocess_end)
                << " frame_total_ms=" << milliseconds(frame_begin, frame_end)
                << " rtsp_client=" << (current_source ? 1 : 0)
                << " depth_p02=" << lo << " depth_p98=" << hi << std::endl;
      ++frame_id;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
  }
  if (sink) gst_object_unref(sink);
  if (capture) {
    gst_element_set_state(capture, GST_STATE_NULL);
    gst_object_unref(capture);
  }
  if (main_loop) {
    g_main_loop_quit(main_loop);
    if (server_thread.joinable()) server_thread.join();
    g_main_loop_unref(main_loop);
  }
  {
    std::lock_guard<std::mutex> lock(rtsp_state.mutex);
    if (rtsp_state.source) gst_object_unref(rtsp_state.source);
    rtsp_state.source = nullptr;
  }
  if (server) gst_object_unref(server);
  if (ctx) rknn_destroy(ctx);
  return result;
}
