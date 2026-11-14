// Checks a document against a PDF/X standard.
//
// This is this project's own check, written without access to the ISO 15930
// texts. Rules whose exact wording is unconfirmed are reported as warnings;
// see HANDOFF.md.
#pragma once

#include "Types.h"

#include <functional>

namespace engine {
class Document;
}

namespace preflight {

// `progress(page, pageCount)` is called before each page; returning false
// stops the check and marks the report as cancelled. Must run on the thread
// that owns the document.
Report run(engine::Document &document, Standard standard, const Options &options = {},
           const std::function<bool(int, int)> &progress = {});

} // namespace preflight
