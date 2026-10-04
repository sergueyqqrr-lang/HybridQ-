#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <cmath>
#include "BandModel.h"

//==============================================================================
/**
    BandProcessor -- Fase 3

    Añade el modo Full/Transient/Sustain (sección 2.2 del brief) sobre la
    base de Fases 1-2 (formas de filtro, M/S, Phase Blend).

    CAMBIO ESTRUCTURAL CLAVE DE ESTA FASE: una banda en modo Transient y la
    MISMA banda en modo Sustain NUNCA pueden compartir el mismo objeto
    `juce::dsp::IIR::Filter` / `juce::dsp::FIR::Filter`, porque esos
    objetos llevan memoria interna (los registros de retardo z^-1) que debe
    ser continua para LA SEÑAL QUE REALMENTE PASA por ellos. Si el mismo
    filtro procesara primero el trozo "transient" y luego el trozo
    "sustain" del mismo bloque, su historia interna se contaminaría entre
    ambas señales. Por eso BandProcessor ahora mantiene DOS `DspChain`
    completas (`chainA`, `chainB`):

      - `chainA` procesa: el modo Transient, O la señal combinada completa
        cuando NINGUNA banda del plugin necesita split (optimización: en
        ese caso ni siquiera se hace el split, cero costo extra sobre
        Fases 1-2).
      - `chainB` procesa: el modo Sustain, y también la mitad "sustain"
        cuando una banda en modo Full convive con otras bandas que sí usan
        Transient/Sustain (ver nota de linealidad abajo).

    LINEALIDAD: cuando el split está activo, una banda en modo Full se
    aplica de forma INDEPENDIENTE a `transientBuffer` (vía chainA) y a
    `sustainBuffer` (vía chainB). Esto es matemáticamente EXACTO, no una
    aproximación: como el filtro es un sistema lineal e invariante en el
    tiempo, filtro(transient) + filtro(sustain) == filtro(transient +
    sustain). Así se evita mantener un tercer buffer "full" por separado
    (el brief sugería 3 señales paralelas; aquí basta con 2 gracias a esta
    propiedad), al costo de que una banda Full procesa el doble de muestras
    mientras el split esté activo en el plugin.
*/
class BandProcessor
{
public:
    BandProcessor();

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void updateCoefficients (double sampleRate, FilterType type,
                              float frequencyHz, float gainDb, float q, float phaseBlend);

    /** Procesa esta banda. `transientBuffer`/`sustainBuffer` son `nullptr`
        cuando NINGUNA banda del plugin necesita el split (optimización de
        Fase 3: el processor solo construye esos buffers si hace falta).
        En ese caso, splitMode DEBE ser Full y se procesa `fullBuffer`
        directamente (comportamiento idéntico a Fases 1-2). Si el split SÍ
        está activo, `fullBuffer` se ignora para esta banda y se opera
        sobre transientBuffer y/o sustainBuffer según splitMode. */
    void process (juce::AudioBuffer<float>& fullBuffer,
                  juce::AudioBuffer<float>* transientBuffer,
                  juce::AudioBuffer<float>* sustainBuffer,
                  ChannelMode channelMode, SplitMode splitMode);

    static float computeMagnitudeForFrequencyDb (double sampleRate, FilterType type,
                                                  float bandFreqHz, float gainDb, float q,
                                                  float evaluateAtHz);

    static constexpr int firDesignSize = 512;
    static constexpr int firLatencySamples = firDesignSize / 2;

    bool isActive = false;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    using FIRFilter = juce::dsp::FIR::Filter<float>;
    using FIRCoeffs = juce::dsp::FIR::Coefficients<float>;

    static void makeCoefficientsForType (double sampleRate, FilterType type,
                                          float frequencyHz, float gainDb, float q,
                                          Coeffs::Ptr& outMain, Coeffs::Ptr& outTilt2);

    void designBlendedFIR (double sampleRate, FilterType type, float frequencyHz,
                            float gainDb, float q, float blend);

    /** Un conjunto completo e independiente de filtros para un "lane" de
        señal (ver nota de la clase). Dos instancias de esto conviven en
        BandProcessor: chainA y chainB. */
    struct DspChain
    {
        Filter laneA, laneB;
        Filter laneATilt2, laneBTilt2;
        FIRFilter firA, firB;
        juce::dsp::DelayLine<float> delayA { firLatencySamples };
        juce::dsp::DelayLine<float> delayB { firLatencySamples };

        void prepare (const juce::dsp::ProcessSpec& monoSpec)
        {
            laneA.prepare (monoSpec);
            laneB.prepare (monoSpec);
            laneATilt2.prepare (monoSpec);
            laneBTilt2.prepare (monoSpec);
            delayA.prepare (monoSpec);
            delayB.prepare (monoSpec);
            delayA.setDelay ((float) firLatencySamples);
            delayB.setDelay ((float) firLatencySamples);
        }

        void reset()
        {
            laneA.reset();
            laneB.reset();
            laneATilt2.reset();
            laneBTilt2.reset();
            firA.reset();
            firB.reset();
            delayA.reset();
            delayB.reset();
        }
    };

    /** Aplica chain a `buffer` con el routing de channelMode habitual
        (Stereo/Left/Right/Mid-Side). Es exactamente la lógica de Fase 1-2,
        factorizada para poder invocarse una o dos veces (una por chain)
        según si el split está activo. */
    void processWithChain (DspChain& chain, juce::AudioBuffer<float>& buffer,
                            ChannelMode channelMode, juce::AudioBuffer<float>& scratch);

    DspChain chainA, chainB;

    Coeffs::Ptr currentCoeffs      { new Coeffs (1, 0, 1, 0) };
    Coeffs::Ptr currentCoeffsTilt2 { new Coeffs (1, 0, 1, 0) };

    FilterType currentType = FilterType::Bell;
    double preparedSampleRate = 44100.0;
    float currentBlend = 0.0f;

    float lastDesignFreq = -1.0f, lastDesignGain = 0.0f, lastDesignQ = -1.0f, lastDesignBlend = -1.0f;
    FilterType lastDesignType = FilterType::Bell;
    double lastDesignSampleRate = -1.0;

    // Compartido entre chainA y chainB -- se usa estrictamente de forma
    // secuencial dentro de una misma llamada a process(), nunca concurrente,
    // así que un único scratch basta (ver processWithChain()).
    juce::AudioBuffer<float> midSideScratch;

    // --- Buffers de trabajo para designBlendedFIR() (ver nota de
    // real-time-safety en el .cpp) -- compartidos entre chains porque el
    // diseño del FIR es independiente de cuál chain lo va a usar.
    juce::dsp::FFT fft { (int) std::log2 ((double) firDesignSize) };
    std::array<float, firDesignSize> fullMagnitude {};
    std::array<float, firDesignSize> logMagnitude {};
    std::array<std::complex<float>, firDesignSize> fftBufferA {};
    std::array<std::complex<float>, firDesignSize> fftBufferB {};
    std::array<float, firDesignSize> minPhase {};
    std::array<float, firDesignSize> windowedCepstrum {};
    std::array<float, firDesignSize> newTaps {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandProcessor)
};
