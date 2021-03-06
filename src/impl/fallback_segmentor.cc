// vim: set sts=2 sw=2 et:
// encoding: utf-8
//
// Copyleft 2011 RIME Developers
// License: GPLv3
//
// 2011-06-20 GONG Chen <chen.sst@gmail.com>
//
#include <rime/common.h>
#include <rime/segmentation.h>
#include <rime/impl/fallback_segmentor.h>

namespace rime {

FallbackSegmentor::FallbackSegmentor(Engine *engine) : Segmentor(engine) {
}

bool FallbackSegmentor::Proceed(Segmentation *segmentation) {
  int len = segmentation->GetCurrentSegmentLength();
  DLOG(INFO) << "current segment length: " << len;
  if (len > 0)
    return false;

  const std::string &input = segmentation->input();
  int k = segmentation->GetCurrentStartPosition();
  DLOG(INFO) << "current start pos: " << k;
  if (k == input.length())
    return false;

  DLOG(INFO) << "segmentation: " << *segmentation;
  if (!segmentation->empty() &&
      segmentation->back().start == segmentation->back().end)
    segmentation->pop_back();
  
  if (!segmentation->empty()) {
    Segment &last(segmentation->back());
    // append one character to the last raw segment
    if (last.HasTag("raw")) {
      last.end = k + 1;
      DLOG(INFO) << "extend previous raw segment to ["
                 << last.start << ", " << last.end << ")";
      // mark redo translation (in case it's been previously translated)
      last.Clear();
      last.tags.insert("raw");
      return false;
    }
  }
  {
    Segment segment;
    segment.start = k;
    segment.end = k + 1;
    DLOG(INFO) << "add a raw segment ["
               << segment.start << ", " << segment.end << ")";
    segment.tags.insert("raw");
    segmentation->Forward();
    segmentation->AddSegment(segment);
  }
  // fallback segmentor should be the last being called, so end this round
  return false;
}

}  // namespace rime
