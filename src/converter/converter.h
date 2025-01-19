// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MOZC_CONVERTER_CONVERTER_H_
#define MOZC_CONVERTER_CONVERTER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "converter/converter_interface.h"
#include "converter/history_reconstructor.h"
#include "converter/immutable_converter_interface.h"
#include "converter/reverse_converter.h"
#include "converter/segments.h"
#include "dictionary/pos_matcher.h"
#include "dictionary/suppression_dictionary.h"
#include "engine/modules.h"
#include "prediction/predictor_interface.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"
#include "testing/friend_test.h"

namespace mozc {

class Converter final : public ConverterInterface {
 public:
  using ImmutableConverterFactory =
      std::function<std::unique_ptr<const ImmutableConverterInterface>(
          const engine::Modules &modules)>;

  using PredictorFactory =
      std::function<std::unique_ptr<prediction::PredictorInterface>(
          const engine::Modules &modules, const ConverterInterface *converter,
          const ImmutableConverterInterface *immutable_converter)>;

  using RewriterFactory = std::function<std::unique_ptr<RewriterInterface>(
      const engine::Modules &modules)>;

  // Converter is initialized with the factory methods of ImmutableConverter,
  // Predictor and Rewriter, so that all these sub components share the
  // same resources and modules. Converter creates these sub modules and holds
  // their ownership.
  Converter(std::unique_ptr<engine::Modules> modules,
            const ImmutableConverterFactory &immutable_converter_factory,
            const PredictorFactory &predictor_factory,
            const RewriterFactory &rewriter_factory);

  ABSL_MUST_USE_RESULT
  bool StartConversion(const ConversionRequest &request,
                       Segments *segments) const override;
  ABSL_MUST_USE_RESULT
  bool StartReverseConversion(Segments *segments,
                              absl::string_view key) const override;
  ABSL_MUST_USE_RESULT
  bool StartPrediction(const ConversionRequest &request,
                       Segments *segments) const override;

  void FinishConversion(const ConversionRequest &request,
                        Segments *segments) const override;
  void CancelConversion(Segments *segments) const override;
  void ResetConversion(Segments *segments) const override;
  void RevertConversion(Segments *segments) const override;

  ABSL_MUST_USE_RESULT
  bool DeleteCandidateFromHistory(const Segments &segments,
                                  size_t segment_index,
                                  int candidate_index) const override;

  ABSL_MUST_USE_RESULT
  bool ReconstructHistory(Segments *segments,
                          absl::string_view preceding_text) const override;

  ABSL_MUST_USE_RESULT
  bool CommitSegmentValue(Segments *segments, size_t segment_index,
                          int candidate_index) const override;
  ABSL_MUST_USE_RESULT
  bool CommitPartialSuggestionSegmentValue(
      Segments *segments, size_t segment_index, int candidate_index,
      absl::string_view current_segment_key,
      absl::string_view new_segment_key) const override;
  ABSL_MUST_USE_RESULT
  bool FocusSegmentValue(Segments *segments, size_t segment_index,
                         int candidate_index) const override;
  ABSL_MUST_USE_RESULT
  bool CommitSegments(Segments *segments,
                      absl::Span<const size_t> candidate_index) const override;
  ABSL_MUST_USE_RESULT bool ResizeSegment(Segments *segments,
                                          const ConversionRequest &request,
                                          size_t segment_index,
                                          int offset_length) const override;
  ABSL_MUST_USE_RESULT bool ResizeSegments(
      Segments *segments, const ConversionRequest &request,
      size_t start_segment_index,
      absl::Span<const uint8_t> new_size_array) const override;

  // Execute ImmutableConverter, Rewriters, SuppressionDictionary.
  // ApplyConversion does not initialize the Segment unlike StartConversion.
  void ApplyConversion(Segments *segments,
                       const ConversionRequest &request) const;

  // Reloads internal data, e.g., user dictionary, etc.
  bool Reload();

  // Synchronizes internal data, e.g., user dictionary, etc.
  bool Sync();

  // Waits for pending operations executed in different threads.
  bool Wait();

  prediction::PredictorInterface *predictor() const { return predictor_.get(); }

  RewriterInterface *rewriter() const { return rewriter_.get(); }

  const ImmutableConverterInterface *immutable_converter() const {
    return immutable_converter_.get();
  }

  engine::Modules *modules() const { return modules_.get(); }

 private:
  FRIEND_TEST(ConverterTest, CompletePosIds);
  FRIEND_TEST(ConverterTest, DefaultPredictor);
  FRIEND_TEST(ConverterTest, MaybeSetConsumedKeySizeToSegment);
  FRIEND_TEST(ConverterTest, PredictSetKey);

  // Complete Left id/Right id if they are not defined.
  // Some users don't push conversion button but directly
  // input hiragana sequence only with composition mode. Converter
  // cannot know which POS ids should be used for these directly-
  // input strings. This function estimates IDs from value heuristically.
  void CompletePosIds(Segment::Candidate *candidate) const;

  bool CommitSegmentValueInternal(Segments *segments, size_t segment_index,
                                  int candidate_index,
                                  Segment::SegmentType segment_type) const;

  // Sets all the candidates' attribute PARTIALLY_KEY_CONSUMED
  // and consumed_key_size if the attribute is not set.
  static void MaybeSetConsumedKeySizeToCandidate(size_t consumed_key_size,
                                                 Segment::Candidate *candidate);

  // Sets all the candidates' attribute PARTIALLY_KEY_CONSUMED
  // and consumed_key_size if the attribute is not set.
  static void MaybeSetConsumedKeySizeToSegment(size_t consumed_key_size,
                                               Segment *segment);

  // Rewrites and applies the suppression dictionary.
  void RewriteAndSuppressCandidates(const ConversionRequest &request,
                                    Segments *segments) const;

  // Limits the number of candidates based on a request.
  // This method doesn't drop meta candidates for T13n conversion.
  void TrimCandidates(const ConversionRequest &request,
                      Segments *segments) const;

  // Commits usage stats for committed text.
  // |begin_segment_index| is a index of whole segments. (history and conversion
  // segments)
  void CommitUsageStats(const Segments *segments, size_t begin_segment_index,
                        size_t segment_length) const;

  // Returns the substring of |str|. This substring consists of similar script
  // type and you can use it as preceding text for conversion.
  bool GetLastConnectivePart(absl::string_view preceding_text, std::string *key,
                             std::string *value, uint16_t *id) const;

  std::unique_ptr<engine::Modules> modules_;
  std::unique_ptr<const ImmutableConverterInterface> immutable_converter_;
  std::unique_ptr<prediction::PredictorInterface> predictor_;
  std::unique_ptr<RewriterInterface> rewriter_;

  const dictionary::PosMatcher &pos_matcher_;
  const dictionary::SuppressionDictionary &suppression_dictionary_;
  const converter::HistoryReconstructor history_reconstructor_;
  const converter::ReverseConverter reverse_converter_;
  const uint16_t general_noun_id_ = std::numeric_limits<uint16_t>::max();
};

}  // namespace mozc

#endif  // MOZC_CONVERTER_CONVERTER_H_
