#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "BandHandleComponent.h"

//==============================================================================
/**
    SpectrumDisplayComponent

    El display central descrito en la sección 4.1: grid de frecuencia/dB,
    espectro en tiempo real (FFT), curva de EQ combinada, y los handles
    arrastrables de cada banda activa.

    Doble click en una zona vacía crea una banda nueva (activa la primera
    banda inactiva disponible, en la frecuencia/gain del click).
*/
class SpectrumDisplayComponent : public juce::Component,
                                  private juce::Timer
{
public:
    explicit SpectrumDisplayComponent (HybridQAudioProcessor& processor);
    ~SpectrumDisplayComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void (int)> onBandSelected;

private:
    void timerCallback() override;

    void drawGrid (juce::Graphics&, juce::Rectangle<float> bounds) const;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> bounds);
    void drawEqCurve (juce::Graphics&, juce::Rectangle<float> bounds) const;
    void rebuildHandlesIfNeeded();

    // --- DIAGNÓSTICO TEMPORAL ------------------------------------------
    // Lista en pantalla con los valores reales (freq/gain/q/type) de cada
    // banda activa, para ver exactamente qué parámetro cambia cuando se
    // toca otra banda. Quitar una vez resuelto el bug de la curva.
    void drawDebugBandValues (juce::Graphics&, juce::Rectangle<float> bounds) const;
    // --------------------------------------------------------------------

    HybridQAudioProcessor& processorRef;

    juce::dsp::FFT fft { HybridQAudioProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) HybridQAudioProcessor::fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };
    std::array<float, HybridQAudioProcessor::fftSize * 2> fftWorkspace {};
    // Magnitudes normalizadas 0..1 (no dB en crudo) listas para dibujar,
    // ya mapeadas al rango visual del scope.
    std::array<float, HybridQAudioProcessor::fftSize / 2> scopeNormalized {};
    bool haveSpectrumData = false;

    std::vector<std::unique_ptr<BandHandleComponent>> handles;
    std::array<bool, maxBands> lastKnownActiveState {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplayComponent)
};
