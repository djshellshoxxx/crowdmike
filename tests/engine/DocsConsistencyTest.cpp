// Documentation consistency (docs/feature-audit/part-C): the README index lists every spec document, no document leaves a
// "future / planned / later / TBD" promise without an explicit out-of-scope note, and the README does not describe the removed
// per-push windows-build release. Reads the repository files relative to this source file; skipped when the tree is not present.
#include "TestHarness.h"
#include <juce_core/juce_core.h>

namespace {
juce::File repoRoot() { return juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory(); }
}

CM_TEST(docs_readme_index_lists_every_spec_document) {
    const auto root = repoRoot();
    const auto readmeFile = root.getChildFile("README.md");
    if (!readmeFile.existsAsFile()) return;
    const auto readme = readmeFile.loadFileAsString();
    CM_EXPECT(readme.contains("docs/feature-audit/"));   // the audit reports are linked as a directory (each audit part adds its own file)
    for (const auto& f : root.getChildFile("docs").findChildFiles(juce::File::findFiles, false, "*.md")) {
        const auto rel = f.getRelativePathFrom(root).replaceCharacter('\\', '/');
        if (!readme.contains(rel)) std::cerr << "README does not link " << rel << '\n';
        CM_EXPECT(readme.contains(rel));
    }
}

CM_TEST(docs_readme_does_not_promise_the_removed_windows_build_release) {
    const auto readmeFile = repoRoot().getChildFile("README.md");
    if (!readmeFile.existsAsFile()) return;
    const auto readme = readmeFile.loadFileAsString();
    CM_EXPECT(!readme.contains("windows-build"));
    CM_EXPECT(readme.contains("RELEASE_PROCESS.md"));
}

CM_TEST(docs_every_future_or_planned_item_is_marked_out_of_scope) {
    const auto root = repoRoot();
    const auto docs = root.getChildFile("docs");
    if (!docs.isDirectory()) return;
    const juce::StringArray words { "future", "planned", "tbd", "todo", "later", "v1.1", "not yet", "if ever" };
    for (const auto& f : docs.findChildFiles(juce::File::findFiles, false, "*.md")) {
        if (f.getFileName() == "POST_BETA_SPEC.md") continue;   // the backlog itself
        const auto lines = juce::StringArray::fromLines(f.loadFileAsString());
        for (int i = 0; i < lines.size(); ++i) {
            const auto lower = lines[i].toLowerCase();
            bool hit = false;
            for (const auto& w : words) {
                const int at = lower.indexOf(w);
                if (at < 0) continue;
                const bool startOk = at == 0 || !juce::CharacterFunctions::isLetterOrDigit(lower[at - 1]);
                const int end = at + w.length();
                const bool endOk = end >= lower.length() || !juce::CharacterFunctions::isLetterOrDigit(lower[end]);
                hit = hit || (startOk && endOk);
            }
            if (!hit) continue;
            const bool marked = lower.contains("out of scope") || lower.contains("post_beta_spec") || lower.contains("fields are only ever added");
            if (!marked) std::cerr << f.getFileName() << ":" << (i + 1) << " has an unmarked future/planned item: " << lines[i] << '\n';
            CM_EXPECT(marked);
        }
    }
}

CM_TEST(docs_post_beta_spec_covers_every_backlog_item) {
    const auto f = repoRoot().getChildFile("docs/POST_BETA_SPEC.md");
    if (!f.existsAsFile()) return;
    const auto text = f.loadFileAsString();
    for (const char* topic : { "Audio Unit", "Auxiliary plug-in buses", "True-peak limiter", "Automatic dynamic notches", "ASIO",
                               "Code signing", "Pitch and time", "64-bit", "Acceptance" })
        CM_EXPECT(text.contains(topic));
}
