#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SpectrumDisplayComponent.h"
#include "BandControlPanel.h"

//==============================================================================
/**
    HybridQAudioProcessorEditor — Fase 1

    Layout: barra superior (nombre, master gain, save/load) + display
    central (espectro/curva/handles) + panel contextual de banda abajo
    (oculto hasta que se selecciona/crea una banda). Corresponde al layout
    descrito en la sección 4.1 del brief.
*/
class HybridQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit HybridQAudioProcessorEditor (HybridQAudioProcessor&);
    ~HybridQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void savePreset();
    void loadPreset();

    HybridQAudioProcessor& processorRef;

    juce::Label titleLabel;

    juce::Slider masterGainSlider;
    juce::Label  masterGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGainAttachment;

    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };

    SpectrumDisplayComponent spectrumDisplay;
    BandControlPanel bandControlPanel;

    std::unique_ptr<juce::FileChooser> activeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridQAudioProcessorEditor)
};
