// CrowdMike standalone application (replaces JUCE's default standalone wrapper window).
#include <juce_audio_utils/juce_audio_utils.h>
#include "DeviceController.h"
#include "PluginProcessor.h"

extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow(const juce::String& name, juce::AudioProcessorEditor* editor, std::function<void()> onClose)
        : juce::DocumentWindow(name, juce::Colour(0xff0f1317), juce::DocumentWindow::allButtons), closeCallback(std::move(onClose)) {
        setUsingNativeTitleBar(true);
        setContentOwned(editor, true);
        setResizable(true, true);
        setResizeLimits(1100, 650, 4096, 2304);
        centreWithSize(juce::jmax(1280, getWidth()), juce::jmax(720, getHeight()));
        setVisible(true);
    }
    void closeButtonPressed() override { closeCallback(); }
private:
    std::function<void()> closeCallback;
};

class CrowdMikeApp final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "CrowdMike"; }
    const juce::String getApplicationVersion() override { return CrowdMikeAudioProcessor::kAppVersion; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
        processor.reset(static_cast<CrowdMikeAudioProcessor*>(createPluginFilter()));
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("CrowdMike");
        devices = std::make_unique<crowdmike::DeviceController>(*processor, folder);
        const auto err = devices->initialise();
        devices->loadLastSession();
        if (devices->restoredAfterCrash())
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "CrowdMike",
                "CrowdMike did not exit cleanly last time. Your last autosaved show has been restored.");
        if (err.isNotEmpty())
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Audio device", err);
        window = std::make_unique<MainWindow>("CrowdMike", processor->createEditor(), [this] { quit(); });
    }

    void shutdown() override {
        window.reset();
        if (devices) devices->shutdownCleanly();
        devices.reset();
        processor.reset();
    }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<CrowdMikeAudioProcessor> processor;
    std::unique_ptr<crowdmike::DeviceController> devices;
    std::unique_ptr<MainWindow> window;
};
}

JUCE_CREATE_APPLICATION_DEFINE(CrowdMikeApp)
