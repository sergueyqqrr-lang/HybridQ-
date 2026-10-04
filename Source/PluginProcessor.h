#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "BandModel.h"
#include "BandProcessor.h"
#include "TransientSustainSplitter.h"

//==============================================================================
/**
    HybridQAudioProcessor — Fase 3

    EQ paramétrico de hasta `maxBands` bandas: forma de filtro, routing de
    canal (Stereo/Mid/Side/Left/Right), Phase Blend continuo (Fase 2), y
    ahora modo Full/Transient/Sustain por banda (Fase 3).

    El split transient/sustain (ver TransientSustainSplitter) solo se
    calcula si AL MENOS UNA banda activa lo necesita (optimización: con
    todas las bandas en Full, el costo es idéntico a Fase 2).

    La latencia reportada al host es fija (ver BandProcessor::firLatencySamples),
    independientemente de cuántas bandas usen blend > 0 -- simplificación
    consciente de Fase 2, sigue aplicando igual aquí.

    Todavía NO incluye (llegan en fases posteriores):
      - Character analógico / Dynamic EQ (Fase 4)
      - Hybrid Adaptive (Fase 5)
*/
class HybridQAudioProcessor : public juce::AudioProcessor
{
public:
    HybridQAudioProcessor();
    ~HybridQAudioProcessor() override;

    //=== AudioProcessor overrides ============================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //=== Parámetros ===========================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    static constexpr const char* masterGainParamID = "masterGain";

    //=== Consulta para la UI ==================================================
    /** Respuesta combinada (dB) de todas las bandas activas a una frecuencia.
        Se usa para dibujar la curva de EQ (sección 4.1). Nota: es una suma
        de magnitudes en dB de cada banda de forma independiente, no una
        multiplicación de respuestas complejas -- aproximación estándar y
        suficiente para un display visual (no se usa para audio). */
    float getFrequencyResponseDb (float frequencyHz) const;

    bool isBandActive (int bandIndex) const;

    //=== Datos para el analizador de espectro (UI) ============================
    static constexpr int fftOrder = 11;               // 2048 puntos
    static constexpr int fftSize  = 1 << fftOrder;

    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    int fifoIndex = 0;
    std::atomic<bool> nextFFTBlockReady { false };

private:
    void pushNextSampleIntoFifo (float sample) noexcept;

    std::array<BandProcessor, maxBands> bands;

    // Punteros cacheados a los parámetros de cada banda, para evitar
    // búsquedas por string en processBlock (real-time safety).
    struct BandParamPtrs
    {
        std::atomic<float>* active = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* channelMode = nullptr;
        std::atomic<float>* phaseBlend = nullptr;
        std::atomic<float>* splitMode = nullptr;
    };
    std::array<BandParamPtrs, maxBands> bandParamPtrs;

    std::atomic<float>* masterGainParam = nullptr;
    juce::SmoothedValue<float> masterGainSmoothed;

    double currentSampleRate = 44100.0;

    // --- Fase 3: split transient/sustain, calculado una sola vez por
    // bloque y reutilizado por todas las bandas que lo necesiten. ---------
    TransientSustainSplitter splitter;
    juce::AudioBuffer<float> transientBuffer, sustainBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridQAudioProcessor)
};
