// The OBS plugin: a filter that taps frames off a source and puts them on a
// socket. It links libobs and nothing else; the ML stack runs in
// card-scanner-server, so a crash there cannot take a live broadcast down.

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include "../obsbridge/FrameClient.h"
#include "ServerProcess.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <memory>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-card-scanner", "en-US")

namespace {

namespace ipc = cardscanner::ipc;
using cardscanner::obsbridge::FrameClient;

/// Owned by the module, not the filter: one server serves every filter.
std::unique_ptr<cardscanner::obsbridge::ServerProcess> g_server;
uint64_t g_token = 0;
std::string g_overlayPath;
std::string g_overlayUrl = "http://127.0.0.1:27847";

constexpr const char *kSettingEnabled = "enabled";
constexpr const char *kSettingFps = "max_fps";
constexpr const char *kSettingRoiX = "roi_x";
constexpr const char *kSettingRoiY = "roi_y";
constexpr const char *kSettingRoiW = "roi_w";
constexpr const char *kSettingRoiH = "roi_h";
constexpr const char *kSettingShowRegion = "show_region";
constexpr const char *kSettingOverlayPath = "overlay_path";
constexpr const char *kSettingDockPath = "dock_path";
constexpr const char *kOverlayName = "Card Scanner Overlay";

struct ScannerFilter {
  obs_source_t *source = nullptr;
  std::unique_ptr<FrameClient> client;

  bool enabled = true;
  double maxFps = 30.0;
  float roiX = 0.25f, roiY = 0.2f, roiW = 0.5f, roiH = 0.6f;

  std::chrono::steady_clock::time_point lastSent{};
  bool showRegion = true;
  bool warnedFormat = false;
};

/// OBS's video_format -> the protocol's. Returns false for anything we do not
/// decode server-side; the caller logs once and passes the frame through.
bool mapFormat(video_format format, ipc::PixelFormat &out, uint32_t &planes) {
  switch (format) {
  case VIDEO_FORMAT_BGRA:
  case VIDEO_FORMAT_BGRX:
    out = ipc::PixelFormat::BGRA;
    planes = 1;
    return true;
  case VIDEO_FORMAT_RGBA:
    out = ipc::PixelFormat::RGBA;
    planes = 1;
    return true;
  case VIDEO_FORMAT_UYVY:
    out = ipc::PixelFormat::UYVY;
    planes = 1;
    return true;
  case VIDEO_FORMAT_YUY2:
    out = ipc::PixelFormat::YUY2;
    planes = 1;
    return true;
  case VIDEO_FORMAT_NV12:
    out = ipc::PixelFormat::NV12;
    planes = 2;
    return true;
  case VIDEO_FORMAT_I420:
    out = ipc::PixelFormat::I420;
    planes = 3;
    return true;
  default:
    return false;
  }
}

/**
 * @brief Draws into the frame so the region is visible in OBS.
 *
 * An async video filter has no render pass, so writing into the frame is the
 * only option - which alters the outgoing video, hence the setting.
 *
 * Only luma is touched, which is the whole pixel (BGRA) or one byte in plane 0
 * for every format accepted here.
 */
void markLuma(obs_source_frame *frame, ipc::PixelFormat format, uint32_t x, uint32_t y) {
  if (x >= frame->width || y >= frame->height) {
    return;
  }
  uint8_t *row = frame->data[0] + size_t(y) * frame->linesize[0];

  switch (format) {
  case ipc::PixelFormat::BGRA:
  case ipc::PixelFormat::RGBA:
    std::memset(row + size_t(x) * 4, 0xF0, 3);
    break;
  case ipc::PixelFormat::UYVY: // U Y V Y - luma is the odd byte
    row[size_t(x) * 2 + 1] = 0xEB;
    break;
  case ipc::PixelFormat::YUY2: // Y U Y V - luma is the even byte
    row[size_t(x) * 2] = 0xEB;
    break;
  case ipc::PixelFormat::NV12:
  case ipc::PixelFormat::I420: // plane 0 is a full-resolution luma plane
    row[x] = 0xEB;
    break;
  default:
    break;
  }
}

void drawRegion(obs_source_frame *frame, ipc::PixelFormat format, float rx, float ry,
                float rw, float rh, uint32_t thick = 2) {
  const auto left = uint32_t(rx * float(frame->width));
  const auto top = uint32_t(ry * float(frame->height));
  const auto right = std::min(uint32_t((rx + rw) * float(frame->width)),
                              frame->width ? frame->width - 1 : 0);
  const auto bottom = std::min(uint32_t((ry + rh) * float(frame->height)),
                               frame->height ? frame->height - 1 : 0);

  // At least two pixels, or it vanishes when OBS scales the preview.
  for (uint32_t thickness = 0; thickness < thick; thickness++) {
    for (uint32_t x = left; x <= right; x++) {
      markLuma(frame, format, x, top + thickness);
      markLuma(frame, format, x, bottom - thickness);
    }
    for (uint32_t y = top; y <= bottom; y++) {
      markLuma(frame, format, left + thickness, y);
      markLuma(frame, format, right - thickness, y);
    }
  }
}

/// Adds the overlay Browser Source to the current scene, sized to the canvas.
/// One source per OBS: a scene that already shows it is left alone, and any
/// other scene gets the same source rather than a second one.
bool addOverlayClicked(obs_properties_t *, obs_property_t *, void *) {
  obs_source_t *sceneSource = obs_frontend_get_current_scene();
  if (sceneSource == nullptr) {
    return false;
  }
  obs_scene_t *scene = obs_scene_from_source(sceneSource);
  obs_source_t *browser = obs_get_source_by_name(kOverlayName);

  if (browser != nullptr &&
      obs_scene_find_source_recursive(scene, kOverlayName) != nullptr) {
    blog(LOG_INFO, "[card-scanner] overlay already in the current scene");
    obs_source_release(browser);
    obs_source_release(sceneSource);
    return false;
  }

  if (browser == nullptr) {
    obs_video_info video = {};
    const bool haveVideo = obs_get_video_info(&video);

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "url", g_overlayUrl.c_str());
    obs_data_set_int(settings, "width", haveVideo ? video.base_width : 1920);
    obs_data_set_int(settings, "height", haveVideo ? video.base_height : 1080);
    // Without this OBS stops rendering the overlay when it is not selected.
    obs_data_set_bool(settings, "shutdown", false);
    browser = obs_source_create("browser_source", kOverlayName, settings, nullptr);
    obs_data_release(settings);
  }

  if (browser != nullptr) {
    obs_sceneitem_t *item = obs_scene_add(scene, browser);
    if (item != nullptr) {
      // obs_scene_add inserts at the bottom of the z-order, which would put the
      // overlay behind the camera it composites over.
      obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
      blog(LOG_INFO, "[card-scanner] added overlay source at %s", g_overlayUrl.c_str());
    } else {
      blog(LOG_WARNING, "[card-scanner] could not add the overlay to the current scene");
    }
    obs_source_release(browser);
  }

  obs_source_release(sceneSource);
  return false;
}

const char *filter_name(void *) { return obs_module_text("CardScanner"); }

void filter_update(void *data, obs_data_t *settings) {
  auto *filter = static_cast<ScannerFilter *>(data);
  filter->enabled = obs_data_get_bool(settings, kSettingEnabled);
  filter->maxFps = obs_data_get_double(settings, kSettingFps);
  filter->roiX = float(obs_data_get_double(settings, kSettingRoiX));
  filter->roiY = float(obs_data_get_double(settings, kSettingRoiY));
  filter->roiW = float(obs_data_get_double(settings, kSettingRoiW));
  filter->roiH = float(obs_data_get_double(settings, kSettingRoiH));
  filter->showRegion = obs_data_get_bool(settings, kSettingShowRegion);

  // A default alone does not survive a scene collection saved before this
  // property existed.
  obs_data_set_string(settings, kSettingOverlayPath, g_overlayUrl.c_str());
  obs_data_set_string(settings, kSettingDockPath, (g_overlayUrl + "/?view=dock").c_str());
}

void *filter_create(obs_data_t *settings, obs_source_t *source) {
  auto *filter = new ScannerFilter();
  filter->source = source;

  // Generated by the module and handed to the server it spawned, so there is
  // nothing to configure or keep in sync.
  filter->client =
      std::make_unique<FrameClient>("127.0.0.1", ipc::kDefaultFramePort, g_token);
  filter->client->start();
  filter_update(filter, settings);

  blog(LOG_INFO, "[card-scanner] filter created, sending to 127.0.0.1:%u",
       unsigned(ipc::kDefaultFramePort));
  return filter;
}

void filter_destroy(void *data) {
  auto *filter = static_cast<ScannerFilter *>(data);
  blog(LOG_INFO, "[card-scanner] filter destroyed (%llu sent, %llu dropped)",
       (unsigned long long)filter->client->sent(),
       (unsigned long long)filter->client->dropped());
  // Shuts the socket down before joining, so this cannot stall the OBS thread
  // tearing the filter down.
  filter->client->stop();
  delete filter;
}

void filter_defaults(obs_data_t *settings) {
  obs_data_set_default_string(settings, kSettingOverlayPath, g_overlayUrl.c_str());
  obs_data_set_default_bool(settings, kSettingEnabled, true);
  obs_data_set_default_bool(settings, kSettingShowRegion, true);
  obs_data_set_default_double(settings, kSettingFps, 30.0);
  // Wider than a card on purpose: the segmentation model needs context around
  // it, and a crop tight to the edges detects nothing.
  obs_data_set_default_double(settings, kSettingRoiX, 0.25);
  obs_data_set_default_double(settings, kSettingRoiY, 0.20);
  obs_data_set_default_double(settings, kSettingRoiW, 0.50);
  obs_data_set_default_double(settings, kSettingRoiH, 0.60);
}

obs_properties_t *filter_properties(void *) {
  obs_properties_t *props = obs_properties_create();
  obs_properties_add_bool(props, kSettingEnabled, obs_module_text("Enable"));
  obs_properties_add_float_slider(props, kSettingFps, obs_module_text("MaxFPS"), 1.0,
                                  60.0, 1.0);
  obs_properties_add_bool(props, kSettingShowRegion, obs_module_text("ShowRegion"));
  obs_properties_add_float_slider(props, kSettingRoiX, obs_module_text("ROI.X"), 0.0,
                                  1.0, 0.01);
  obs_properties_add_float_slider(props, kSettingRoiY, obs_module_text("ROI.Y"), 0.0,
                                  1.0, 0.01);
  obs_properties_add_float_slider(props, kSettingRoiW, obs_module_text("ROI.Width"),
                                  0.05, 1.0, 0.01);
  obs_properties_add_float_slider(props, kSettingRoiH, obs_module_text("ROI.Height"),
                                  0.05, 1.0, 0.01);

  // Shown here so adding the Browser Source does not mean hunting for a URL.
  obs_properties_add_text(props, kSettingOverlayPath, obs_module_text("OverlayPath"),
                          OBS_TEXT_INFO);
  // OBS exposes only Qt widget docks to plugins, and linking Qt would put a
  // heavyweight dependency back in OBS's process - so the URL is shown for
  // OBS's own Docks > Custom Browser Docks.
  obs_properties_add_text(props, kSettingDockPath, obs_module_text("DockPath"),
                          OBS_TEXT_INFO);
  // One click instead of asking the user to add a Browser Source and paste a
  // URL into it. The overlay used to live inside this bundle as a file, which
  // no file dialog can reach: macOS treats a .plugin as a package.
  obs_properties_add_button(props, "add_overlay", obs_module_text("AddOverlay"),
                            addOverlayClicked);
  return props;
}

/// The tap. Runs on the source's thread, so everything here is bounded: a
/// format check, a rate check, and a memcpy into a mailbox.
obs_source_frame *filter_video(void *data, obs_source_frame *frame) {
  auto *filter = static_cast<ScannerFilter *>(data);
  if (!filter->enabled || frame == nullptr) {
    return frame;
  }

  ipc::PixelFormat drawFormat = ipc::PixelFormat::Unknown;
  uint32_t drawPlanes = 0;
  if (filter->showRegion && mapFormat(frame->format, drawFormat, drawPlanes)) {
    // Every frame, not only sampled ones, or it strobes against the video.
    drawRegion(frame, drawFormat, filter->roiX, filter->roiY, filter->roiW,
               filter->roiH);

    // Thicker once accepted, so a confirmed card reads differently at a
    // glance from one still being considered.
    float bx = 0, by = 0, bw = 0, bh = 0;
    bool accepted = false;
    if (filter->client->latestDetection(bx, by, bw, bh, accepted)) {
      drawRegion(frame, drawFormat, bx, by, bw, bh, accepted ? 4 : 2);
    }
  }

  // The server throttles too, but there is no point copying frames it will
  // discard.
  const auto now = std::chrono::steady_clock::now();
  const auto minGap = std::chrono::milliseconds(
      int(1000.0 / (filter->maxFps > 0.0 ? filter->maxFps : 30.0)));
  if (now - filter->lastSent < minGap) {
    return frame;
  }

  ipc::PixelFormat format = ipc::PixelFormat::Unknown;
  uint32_t planeCount = 0;
  if (!mapFormat(frame->format, format, planeCount)) {
    if (!filter->warnedFormat) {
      filter->warnedFormat = true;
      blog(LOG_WARNING,
           "[card-scanner] unsupported pixel format %d; frames will not be scanned",
           int(frame->format));
    }
    return frame;
  }

  // Chroma planes are half height in NV12 and I420. linesize comes from OBS
  // because camera buffers are commonly padded.
  uint32_t linesize[ipc::kMaxPlanes] = {0};
  uint32_t rows[ipc::kMaxPlanes] = {0};
  const uint8_t *planes[ipc::kMaxPlanes] = {nullptr};
  for (uint32_t i = 0; i < planeCount; i++) {
    planes[i] = frame->data[i];
    linesize[i] = frame->linesize[i];
    rows[i] = (i == 0) ? frame->height : frame->height / 2;
    if (planes[i] == nullptr) {
      return frame;
    }
  }

  ipc::FrameHeader header{};
  header.format = uint32_t(format);
  header.width = frame->width;
  header.height = frame->height;
  header.planeCount = planeCount;
  header.roiX = filter->roiX;
  header.roiY = filter->roiY;
  header.roiWidth = filter->roiW;
  header.roiHeight = filter->roiH;

  filter->client->submit(header, planes, linesize, rows);
  filter->lastSent = now;
  return frame;
}

obs_source_info makeFilterInfo() {
  obs_source_info info = {};
  info.id = "card_scanner_filter";
  info.type = OBS_SOURCE_TYPE_FILTER;
  // ASYNC_VIDEO gets us filter_video with CPU-side frames, which is what a
  // camera produces. No GPU readback, and nothing on the graphics thread.
  info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
  info.get_name = filter_name;
  info.create = filter_create;
  info.destroy = filter_destroy;
  info.update = filter_update;
  info.get_defaults = filter_defaults;
  info.get_properties = filter_properties;
  info.filter_video = filter_video;
  return info;
}

obs_source_info g_filterInfo = makeFilterInfo();

} // namespace

MODULE_EXPORT const char *obs_module_description(void) {
  return "Scans trading cards in a video source and drives an overlay.";
}

bool obs_module_load(void) {
  obs_register_source(&g_filterInfo);

  // Everything the server needs ships inside this bundle.
  char *server = obs_module_file("card-scanner-server");
  char *config = obs_module_file("config.json");
  char *overlay = obs_module_file("overlay.html");
  g_overlayPath = overlay ? overlay : "";

  if (server && config) {
    std::random_device rd;
    g_token = (uint64_t(rd()) << 32) | rd();

    // OBS's own config directory, so this lands beside the OBS logs on every
    // platform.
    std::string log;
    if (char *configDir = obs_module_config_path(nullptr)) {
      os_mkdirs(configDir);
      log = std::string(configDir) + "/card-scanner-server.log";
      bfree(configDir);
    } else {
      log = (std::filesystem::temp_directory_path() / "card-scanner-server.log").string();
    }

    g_server = std::make_unique<cardscanner::obsbridge::ServerProcess>(
        server, config, log, g_token, ipc::kDefaultFramePort,
        ipc::kDefaultControlPort);
    g_server->start();
    blog(LOG_INFO, "[card-scanner] server starting, log: %s", log.c_str());
  } else {
    blog(LOG_ERROR,
         "[card-scanner] bundle is missing card-scanner-server or config.json; "
         "scanning will not work");
  }

  bfree(server);
  bfree(config);
  bfree(overlay);

  blog(LOG_INFO, "[card-scanner] module loaded, overlay served at %s",
       g_overlayUrl.c_str());
  return true;
}

void obs_module_unload(void) {
  // Without this the server outlives OBS holding both ports, and the next
  // launch cannot bind them.
  g_server.reset();
  blog(LOG_INFO, "[card-scanner] module unloaded");
}
