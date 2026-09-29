#pragma once

#include <juce_dsp/juce_dsp.h>
#include "BandModel.h"

//==============================================================================
/**
    BandProcessor

    Fase 1: procesamiento de una única banda en modo minimum-phase (biquad,
    juce::dsp::IIR). No hay todavía Phase Blend (Fase 2) ni Transient/Sustain
    (Fase 3) -- cada banda procesa "Full" siempre en esta fase.

    Internamente mantiene DOS filtros IIR independientes ("lane A" / "lane B"),
    porque cada lane necesita su propio estado interno (memoria del filtro)
    aunque compartan coeficientes:
      - ChannelMode::Stereo        -> laneA = canal L, laneB = canal R
      - ChannelMode::Mid/Side      -> solo se usa laneA (Mid o Side, según mode)
      - ChannelMode::Left/Right    -> solo se usa laneA (el canal elegido)

    Para TiltShelf, laneA/laneB en realidad contienen DOS secciones en
    cascada (low-shelf + high-shelf con ganancias opuestas) -- ver
    updateCoefficients().
*/
class BandProcessor
{
public:
    BandProcessor() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Recalcula coeficientes a partir de los valores actuales de parámetro.
        Se llama una vez por bloque (no por sample) desde el processor. */
    void updateCoefficients (double sampleRate, FilterType type,
                              float frequencyHz, float gainDb, float q);

    /** Procesa el buffer estéreo completo, aplicando encode/decode M-S
        internamente si el channelMode lo requiere. `buffer` se modifica
        in-place. */
    void process (juce::AudioBuffer<float>& buffer, ChannelMode channelMode);

    /** Versión "libre de estado", pensada explícitamente para ser llamada
        desde el hilo de UI: no toca ningún miembro de la instancia
        (laneA/laneB/currentCoeffs), construye sus propios coeficientes
        temporales en variables locales y los descarta al salir. Es la
        forma segura de calcular la curva de EQ para el display sin
        compartir el objeto de coeficientes real que usa el hilo de audio
        (compartirlo causaba una condición de carrera: el hilo de audio
        podía liberar el objeto viejo justo cuando la UI lo estaba leyendo,
        provocando un crash al arrastrar un handle rápidamente).

        `bandFreqHz` es la frecuencia propia de la banda (su parámetro
        Freq); `evaluateAtHz` es el punto del eje X de la curva en el que
        se quiere conocer la respuesta -- son dos cosas distintas: para
        una banda Bell centrada en 1 kHz, se llama una vez por cada X del
        display (20 Hz, 21 Hz, ... 20 kHz) con bandFreqHz siempre = 1000. */
    static float computeMagnitudeForFrequencyDb (double sampleRate, FilterType type,
                                                  float bandFreqHz, float gainDb, float q,
                                                  float evaluateAtHz);

    bool isActive = false;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    /** Construye los coeficientes para una forma de filtro dada. Factorizado
        para que tanto updateCoefficients() (hilo de audio, muta el estado
        real de la banda) como computeMagnitudeForFrequencyDb() (cualquier
        hilo, variables 100% locales) compartan la misma lógica sin
        compartir el mismo objeto en memoria. */
    static void makeCoefficientsForType (double sampleRate, FilterType type,
                                          float frequencyHz, float gainDb, float q,
                                          Coeffs::Ptr& outMain, Coeffs::Ptr& outTilt2);

    // Sección principal (todas las formas excepto Tilt).
    Filter laneA, laneB;

    // Segunda sección, solo usada por TiltShelf (cascada low-shelf+high-shelf).
    Filter laneATilt2, laneBTilt2;

    Coeffs::Ptr currentCoeffs      { new Coeffs (1, 0, 1, 0) };
    Coeffs::Ptr currentCoeffsTilt2 { new Coeffs (1, 0, 1, 0) };

    FilterType currentType = FilterType::Bell;
    double preparedSampleRate = 44100.0;

    // Buffers de trabajo para el encode/decode M-S (evitan allocs en
    // processBlock: se redimensionan solo en prepare()).
    juce::AudioBuffer<float> midSideScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandProcessor)
};
