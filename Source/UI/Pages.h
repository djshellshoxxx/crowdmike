#pragma once
// Page factories. Each page lives in UI/Pages/<Name>Page.cpp and defines exactly one factory below.
#include <memory>
#include "UI/EditorContext.h"

namespace crowdmike::ui {
std::unique_ptr<Page> makeLivePage(EditorContext&);
std::unique_ptr<Page> makeInputsPage(EditorContext&);
std::unique_ptr<Page> makeGroupsPage(EditorContext&);
std::unique_ptr<Page> makeFxPage(EditorContext&);
std::unique_ptr<Page> makeMatrixPage(EditorContext&);
std::unique_ptr<Page> makeRecordPage(EditorContext&);
std::unique_ptr<Page> makePlaybackPage(EditorContext&);
std::unique_ptr<Page> makeScenesPage(EditorContext&);
std::unique_ptr<Page> makeSettingsPage(EditorContext&);
std::unique_ptr<Page> makeDiagnosticsPage(EditorContext&);
}
