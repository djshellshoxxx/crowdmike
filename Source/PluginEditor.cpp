#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff111519 };
const juce::Colour panel { 0xff1b2228 };
const juce::Colour accent { 0xff55d6be };
const juce::Colour text { 0xffe8eef1 };
const juce::Colour mutedText { 0xff9aabb4 };

juce::String parameterId(int input, const juce::String& suffix)
{
    return "input" + juce::String(input + 1) + "." + suffix;
}

void styleSlider(juce::Slider& slider, const juce::String& name, const juce::String& suffix)
{
    slider.setName(name);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 19);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::trackColourId, accent);
    slider.setColour(juce::Slider::thumbColourId, accent);
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, panel);
    slider.setColour(juce::Slider::textBoxOutlineColourId, panel);
    slider.setNumDecimalPlacesToDisplay(suffix == " dB" ? 1 : 0);
}

class PeakMeter final : public juce::Component
{
public:
    void setLevel(float next) noexcept
    {
        level = juce::jmax(next, level * 0.82f);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour { 0xff0c1013 });
        g.fillRoundedRectangle(bounds, 3.0f);
        bounds = bounds.reduced(2.0f);
        const auto amount = juce::jlimit(0.0f, 1.0f, level);
        if (amount <= 0.0f)
            return;
        const float fillHeight = bounds.getHeight() * amount;
        auto fill = bounds.removeFromBottom(fillHeight);
        g.setColour(amount >= 0.94f ? juce::Colour { 0xffff625d } : accent);
        g.fillRoundedRectangle(fill, 2.0f);
    }

private:
    float level = 0.0f;
};
}

class CrowdMikeAudioProcessorEditor::InputControls final : public juce::Component
{
public:
    InputControls(juce::AudioProcessorValueTreeState& state, int inputIndex)
    {
        title.setText("INPUT " + juce::String(inputIndex + 1), juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, accent);
        title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        title.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(title);

        trim.setRange(-60.0, 24.0, 0.01);
        highPassHz.setRange(20.0, 500.0, 1.0);
        lowPassHz.setRange(2000.0, 20000.0, 1.0);
        styleSlider(trim, "Input trim", " dB");
        styleSlider(highPassHz, "High-pass cutoff", " Hz");
        styleSlider(lowPassHz, "Low-pass cutoff", " Hz");
        addAndMakeVisible(trim);
        addAndMakeVisible(highPassHz);
        addAndMakeVisible(lowPassHz);

        mute.setButtonText("MUTE");
        mute.setTooltip("Mute this input");
        polarity.setButtonText("POL");
        polarity.setTooltip("Invert this input polarity");
        highPass.setButtonText("HP");
        highPass.setTooltip("Enable high-pass filter");
        lowPass.setButtonText("LP");
        lowPass.setTooltip("Enable low-pass filter");
        for (auto* button : { &mute, &polarity, &highPass, &lowPass })
        {
            button->setColour(juce::ToggleButton::textColourId, text);
            button->setColour(juce::ToggleButton::tickColourId, accent);
            addAndMakeVisible(*button);
        }
        addAndMakeVisible(meter);

        const auto prefix = parameterId(inputIndex, "");
        trimAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            state, prefix + "trimDb", trim);
        muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            state, prefix + "mute", mute);
        polarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            state, prefix + "polarity", polarity);
        highPassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            state, prefix + "highPassEnabled", highPass);
        highPassHzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            state, prefix + "highPassHz", highPassHz);
        lowPassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            state, prefix + "lowPassEnabled", lowPass);
        lowPassHzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            state, prefix + "lowPassHz", lowPassHz);
    }

    void setPeak(float peak) noexcept { meter.setLevel(peak); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 7.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(20));
        area.removeFromTop(2);
        auto buttonRow = area.removeFromTop(22);
        const int buttonWidth = (buttonRow.getWidth() - 3) / 4;
        mute.setBounds(buttonRow.removeFromLeft(buttonWidth));
        buttonRow.removeFromLeft(1);
        polarity.setBounds(buttonRow.removeFromLeft(buttonWidth));
        buttonRow.removeFromLeft(1);
        highPass.setBounds(buttonRow.removeFromLeft(buttonWidth));
        buttonRow.removeFromLeft(1);
        lowPass.setBounds(buttonRow);
        area.removeFromTop(3);

        auto sliderArea = area.reduced(0, 0);
        sliderArea.removeFromRight(10);
        const int rowHeight = sliderArea.getHeight() / 3;
        trim.setBounds(sliderArea.removeFromTop(rowHeight));
        highPassHz.setBounds(sliderArea.removeFromTop(rowHeight));
        lowPassHz.setBounds(sliderArea.removeFromTop(rowHeight));
        meter.setBounds(getWidth() - 12, 28, 5, juce::jmax(8, getHeight() - 36));
    }

private:
    juce::Label title;
    juce::Slider trim, highPassHz, lowPassHz;
    juce::ToggleButton mute, polarity, highPass, lowPass;
    PeakMeter meter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> polarityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> highPassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highPassHzAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowPassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowPassHzAttachment;
};

CrowdMikeAudioProcessorEditor::~CrowdMikeAudioProcessorEditor() = default;

CrowdMikeAudioProcessorEditor::CrowdMikeAudioProcessorEditor(CrowdMikeAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    title.setText("CROWDMIKE  /  LIVE INPUTS", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, text);
    title.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    addAndMakeVisible(title);

    limiterLabel.setText("LIMITER", juce::dontSendNotification);
    limiterLabel.setColour(juce::Label::textColourId, mutedText);
    limiterLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addAndMakeVisible(limiterLabel);
    styleSlider(limiterSlider, "Limiter ceiling", " dB");
    limiterSlider.setRange(-12.0, 0.0, 0.1);
    addAndMakeVisible(limiterSlider);
    limiterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getParameters(), "limiterCeilingDb", limiterSlider);

    for (int input = 0; input < crowdmike::RoutingMatrix::maxChannels; ++input)
    {
        auto* controls = new InputControls(processor.getParameters(), input);
        inputControls.add(controls);
        addAndMakeVisible(controls);
    }

    setResizable(true, true);
    setResizeLimits(960, 640, 2400, 1600);
    setSize(1280, 800);
    startTimerHz(30);
}

void CrowdMikeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
}

void CrowdMikeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto header = area.removeFromTop(34);
    title.setBounds(header.removeFromLeft(header.getWidth() - 310));
    limiterLabel.setBounds(header.removeFromLeft(62));
    limiterSlider.setBounds(header);

    area.removeFromTop(10);
    const int gap = 8;
    const int columnWidth = (area.getWidth() - gap * 3) / 4;
    const int rowHeight = (area.getHeight() - gap * 3) / 4;
    for (int i = 0; i < inputControls.size(); ++i)
    {
        const int row = i / 4;
        const int column = i % 4;
        inputControls[i]->setBounds(area.getX() + column * (columnWidth + gap),
                                    area.getY() + row * (rowHeight + gap),
                                    columnWidth, rowHeight);
    }
}

void CrowdMikeAudioProcessorEditor::timerCallback()
{
    for (int i = 0; i < inputControls.size(); ++i)
        inputControls[i]->setPeak(processor.getAndResetInputPeak(i));
}
