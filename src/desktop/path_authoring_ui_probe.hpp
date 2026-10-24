#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json pathAuthoringRetainedState(const DocumentSnapshot &);
void seedPathAuthoringRetainedMetadata(DocumentData &);
void seedPathAuthoringRetainedDisplay(DisplayState &);
void writePathAuthoringObservation(const std::string &, Json &, std::size_t maximum_bytes);
Json pathAuthoringUiCase(Window &, const std::string &mode,
                        const std::string &context, const std::string &fresh_directory);
}
