#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "BandModel.h"

//==============================================================================
/**
    BandControlPanel

    Panel contextual descrito en la sección 4.1/4.2: aparece con los
    controles esenciales de la banda seleccionada. En Fase 1 expone
    freq/gain/Q/shape/channel/active/delete -- el selector Full/Transient/
    Sustain y el slider de Phase Blend se añaden en Fases 2-3 (sección 4.2),
    dejando ya el hueco reservado en el layout (ver comentario en resized()).
*/
class BandControlPanel : public juce::Component
{
public:
    explicit BandControlPanel (HybridQAudioProcessor& processor);

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Cambia qué banda muestra el panel; -1 = ninguna (panel oculto). */
    void setSelectedBand (int bandIndex);

private:
    void rebuildAttachments();

    HybridQAudioProcessor& processorRef;
    int currentBandIndex = -1;

    juce::Label bandTitleLabel;

    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label  freqLabel, gainLabel, qLabel;

    juce::ComboBox typeBox, channelBox;
    juce::Label typeLabel, channelLabel;

    juce::ToggleButton activeToggle { "Active" };
    juce::TextButton deleteButton { "Delete Band" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment, channelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandControlPanel)
};
