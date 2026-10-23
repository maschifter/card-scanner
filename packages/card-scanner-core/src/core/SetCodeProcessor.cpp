#include "SetCodeProcessor.h"
#include "../Constants.h"
#include "../benchmark/BenchmarkCollector.h"
#include "../models/ygo/SetCodeReader.h"
#include <Log.h>
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <vector>

namespace cardscanner {
namespace core {

namespace {

// SetCodeReader (c20) search boxes, fractions of the card: x0, y0, x1, y1.
// minLastStep: the step of 64 a code's last character reaches at least.
// Regular codes are right-aligned and end at step 46+; a Link code seen
// through the regular box ends by step 44 (800 dev cards).
struct LayoutBox {
  const char *name;
  float x0, y0, x1, y1;
  int minLastStep;
};
constexpr LayoutBox REGULAR = {"regular", 0.668f, 0.689f, 0.937f, 0.781f, 45};
constexpr LayoutBox LINK = {"link", 0.610f, 0.673f, 0.877f, 0.772f, 0};
constexpr LayoutBox PENDULUM = {"pendulum", 0.036f, 0.889f, 0.303f, 0.967f, 0};

// Measured on 500 labeled cards: the code's center sits 0.0211 of the card
// height above the text box's top border (5th-95th percentile -0.023 to
// -0.010). Re-centering the regular box there recovers most cards a bad
// straightening moved: 93.0% -> 97.8% at +-4% vertical error.
constexpr float CODE_ABOVE_TEXT_BOX = 0.0211f;

// The band searched for the text box's top border, and what counts as a long
// line: straight for 30% of the card, covering 20% of a row after the open.
constexpr float LINE_BAND_TOP = 0.58f, LINE_BAND_BOTTOM = 0.86f;
constexpr float LINE_KERNEL = 0.30f, LINE_COVERAGE = 0.20f;
// A double border reads as two lines about 1% apart; the code sits above the
// upper one.
constexpr float DOUBLE_BORDER = 0.015f;

/// "YGLD-ENA22", "ME-10", "15AY-JPB15": one dash, a 2-5 character prefix, a
/// 2-7 character rest holding at least one digit.
bool looksLikeCode(const std::string &text) {
  const auto dash = text.find('-');
  if (dash == std::string::npos || text.find('-', dash + 1) != std::string::npos) {
    return false;
  }
  const std::string rest = text.substr(dash + 1);
  return dash >= 2 && dash <= 5 && rest.size() >= 2 && rest.size() <= 7 &&
         std::any_of(rest.begin(), rest.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

// A box cut too far right or left clips the code: Link codes sit left of the
// regular box, so it reads BLCR-EN045 as LCR-EN045, starting at step 0-3 of
// 64. In its own box a code starts at step 6 or later and ends by step 58
// (1,000 dev cards).
constexpr int FIRST_CLEAR_STEP = 5;
constexpr int LAST_CLEAR_STEP = 61;

/// The read's first or last character sits at the box's edge, or the read
/// ends before the layout's codes do.
bool looksClipped(const CtcRead &r, int minLastStep) {
  return r.firstStep >= 0 &&
         (r.firstStep < FIRST_CLEAR_STEP || r.lastStep > LAST_CLEAR_STEP ||
          r.lastStep < minLastStep);
}

cv::Rect toPixels(const cv::Rect2f &box, const cv::Size &size) {
  const cv::Rect rect(cvRound(box.x * size.width), cvRound(box.y * size.height),
                      cvRound(box.width * size.width),
                      cvRound(box.height * size.height));
  return rect & cv::Rect(0, 0, size.width, size.height);
}

} // namespace

float SetCodeProcessor::textBoxTop(const cv::Mat &gray) {
  const int h = gray.rows, w = gray.cols;
  const int top = cvRound(LINE_BAND_TOP * h), bottom = cvRound(LINE_BAND_BOTTOM * h);
  if (bottom - top < 4 || w < 16) {
    return -1.f;
  }
  cv::Mat blurred, dy;
  cv::GaussianBlur(gray(cv::Range(top, bottom), cv::Range::all()), blurred,
                   {3, 3}, 0);
  cv::Sobel(blurred, dy, CV_32F, 0, 1, 3);
  dy = cv::abs(dy);

  // Keep the strongest 10% of horizontal edges.
  std::vector<float> values(dy.begin<float>(), dy.end<float>());
  const auto cut = values.begin() + static_cast<long>(values.size() * 0.9);
  std::nth_element(values.begin(), cut, values.end());
  cv::Mat edges = dy > *cut;

  // Only edges running straight for 30% of the card survive: frame borders
  // do, rules text and art detail don't.
  cv::morphologyEx(edges, edges, cv::MORPH_OPEN,
                   cv::getStructuringElement(
                       cv::MORPH_RECT, {std::max(3, cvRound(LINE_KERNEL * w)), 1}));

  // Rows covered well enough, merged into lines (a border is 2-4 rows).
  struct Line {
    int first, last;
    float strength;
  };
  std::vector<Line> lines;
  for (int y = 0; y < edges.rows; ++y) {
    const float coverage = cv::countNonZero(edges.row(y)) / float(w);
    if (coverage <= LINE_COVERAGE) {
      continue;
    }
    if (!lines.empty() && y - lines.back().last <= 2) {
      lines.back().last = y;
      lines.back().strength = std::max(lines.back().strength, coverage);
    } else {
      lines.push_back({y, y, coverage});
    }
  }
  if (lines.empty()) {
    return -1.f;
  }
  auto center = [&](const Line &l) { return (top + (l.first + l.last) / 2.f) / h; };
  size_t best = std::max_element(lines.begin(), lines.end(),
                                 [](const Line &a, const Line &b) {
                                   return a.strength < b.strength;
                                 }) -
                lines.begin();
  while (best > 0 && center(lines[best]) - center(lines[best - 1]) < DOUBLE_BORDER) {
    --best;
  }
  return center(lines[best]);
}

SetCodeInfo SetCodeProcessor::processSetCode(
    const cv::Mat &cardImage, const std::vector<CardSearchResult> &cardMatches,
    float disambiguationThreshold, SetCodeReader *reader) {
  if (reader == nullptr || cardImage.empty() ||
      !isAmbiguous(cardMatches, disambiguationThreshold)) {
    return SetCodeInfo();
  }
  try {
    return read(cardImage, *reader);
  } catch (const std::exception &e) {
    log(LOG_LEVEL::Error, "[CardScanner]", "set code read failed:", e.what());
    return SetCodeInfo();
  }
}

bool SetCodeProcessor::isBetter(const CtcRead &read, const CtcRead &than) {
  // Code-shaped, then any text, then nothing. An empty read is confident
  // (every step is a sure blank), so confidence only breaks ties in a tier.
  const auto tier = [](const CtcRead &r) {
    return looksLikeCode(r.text) ? 2 : r.text.empty() ? 0 : 1;
  };
  const int a = tier(read), b = tier(than);
  return a != b ? a > b : read.confidence > than.confidence;
}

SetCodeInfo SetCodeProcessor::read(const cv::Mat &rgbCard,
                                   SetCodeReader &reader) {
  using benchmark::BenchmarkCollector;
  using benchmark::Stage;
  SetCodeInfo info;
  if (rgbCard.empty()) {
    return info;
  }
  cv::Mat card;
  {
    BenchmarkCollector::ScopedTimer timer(Stage::SetCodeCardPrep);
    // The search box fractions assume a card the dewarp's height.
    const int height = constants::card::DEWARP_HEIGHT;
    cv::resize(rgbCard, card,
               {cvRound(rgbCard.cols * double(height) / rgbCard.rows), height},
               0, 0, rgbCard.rows > height ? cv::INTER_AREA : cv::INTER_LINEAR);
  }

  bool found = false;
  auto attempt = [&](const cv::Rect2f &box, const char *layout,
                     int minLastStep) {
    const cv::Rect pixels = toPixels(box, card.size());
    if (pixels.width < 8 || pixels.height < 8) {
      return;
    }
    CtcRead read = reader.read(card(pixels));
    BenchmarkCollector::add(benchmark::Metric::SetCodeReads, 1);
    BenchmarkCollector::ScopedTimer postTimer(Stage::SetCodePost);
    const bool code =
        looksLikeCode(read.text) && !looksClipped(read, minLastStep);
    // Until a read is found, keep the best so far.
    if (code || isBetter(read, info.read)) {
      info = {std::move(read), layout, box};
      found = code;
    }
  };
  auto asRect = [](const LayoutBox &b) {
    return cv::Rect2f(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0);
  };

  attempt(asRect(REGULAR), REGULAR.name, REGULAR.minLastStep);
  if (!found) {
    float line;
    {
      BenchmarkCollector::ScopedTimer timer(Stage::SetCodeLineFinder);
      cv::Mat gray;
      cv::cvtColor(card, gray, cv::COLOR_RGB2GRAY);
      line = textBoxTop(gray);
    }
    if (line > 0) {
      cv::Rect2f anchored = asRect(REGULAR);
      anchored.y = line - CODE_ABOVE_TEXT_BOX - anchored.height / 2;
      attempt(anchored, "anchored", REGULAR.minLastStep);
    }
  }
  if (!found) attempt(asRect(LINK), LINK.name, LINK.minLastStep);
  if (!found) attempt(asRect(PENDULUM), PENDULUM.name, PENDULUM.minLastStep);
  BenchmarkCollector::set(benchmark::Label::SetCodeText,
                          found ? info.read.text : "(" + info.read.text + ")");
  return info;
}

} // namespace core
} // namespace cardscanner
