# HybridQ — Fase 0 (esqueleto funcional)

Este es el punto de partida del roadmap: un plugin que compila, carga en un
DAW/host, pasa audio sin artefactos, y expone **un** parámetro automatizable
(`Test Gain`) de punta a punta (host ⇄ APVTS ⇄ audio thread ⇄ UI).

No hay todavía: bandas, EQ real, Transient/Sustain, Phase Blend, ni Character.
Eso llega en las Fases 1-5. El objetivo de esta fase es exclusivamente
**validar que la fontanería del plugin es sólida** antes de meter DSP complejo.

## CI en GitHub

El repo incluye `.github/workflows/build.yml`: compila automáticamente en
Ubuntu, macOS y Windows en cada push/PR a `main`, y deja el VST3 (y el
Standalone) descargable como artifact desde la pestaña **Actions** de
GitHub — útil para probar un build sin tener que compilar en local.

Para que funcione tal cual, sube el proyecto a un repo con esta estructura
en la raíz (`CMakeLists.txt`, `Source/`, `.github/workflows/build.yml`).
No hace falta ninguna configuración adicional: JUCE se descarga solo vía
`FetchContent` la primera vez que corre el workflow (y queda cacheado
para los siguientes runs).

## Requisitos

- CMake ≥ 3.22
- Un compilador C++20 (Clang/MSVC/GCC recientes)
- macOS: Xcode command line tools instaladas
- Windows: Visual Studio 2022 (con el workload de desarrollo de escritorio C++)
- Linux: paquetes de desarrollo de JUCE (ALSA/X11/etc. — ver la documentación
  de JUCE, `docs/README.md` en el repo de JUCE, sección Linux Dependencies)

No necesitas descargar JUCE a mano: `CMakeLists.txt` usa `FetchContent` para
traerlo automáticamente desde GitHub la primera vez que configures el proyecto.
Si prefieres fijar JUCE como submódulo local (recomendado para iteración diaria,
evita re-descargar y te permite pinnear un commit exacto), comenta el bloque
`FetchContent` en `CMakeLists.txt` y usa en su lugar:

```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
```

junto con `add_subdirectory(JUCE)`.

## Compilar

```bash
cmake -B build -S .
cmake --build build --config Release
```

Los binarios (VST3 y Standalone en esta fase; AU requiere macOS y AAX requiere
el SDK de Avid, ambos se añaden en fases posteriores) quedarán copiados
automáticamente en las carpetas estándar de plugins de tu sistema gracias a
`COPY_PLUGIN_AFTER_BUILD TRUE`.

## Validar (criterio de salida de Fase 0)

1. Carga el VST3 en tu DAW de prueba (Reaper es el más rápido para iterar).
2. Pasa audio real por el plugin: debe sonar **idéntico** a bypass.
3. Automatiza el knob "Test Gain" desde el DAW: debe responder sin clicks ni
   glitches, y el valor debe recuperarse correctamente al guardar/recargar
   el proyecto del DAW (prueba `getStateInformation`/`setStateInformation`).
4. Corre `pluginval` (https://github.com/Tracktion/pluginval) sobre el binario
   generado — debe pasar sin warnings ni crashes:
   ```bash
   pluginval --validate build/HybridQ_artefacts/Release/VST3/HybridQ.vst3
   ```

Si los 4 puntos pasan, Fase 0 está cerrada y se puede avanzar a Fase 1
(EQ paramétrico clásico — bandas reales, formas de filtro, display de
espectro) siguiendo el roadmap.

## Fase 1 — EQ paramétrico clásico (ya incluida en este código)

Añadido sobre la Fase 0:

- Hasta 24 bandas (`maxBands` en `BandModel.h`), cada una con:
  - Forma de filtro: Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt.
  - Routing de canal: Stereo, Mid, Side, Left, Right (encode/decode M-S por banda).
  - Procesamiento minimum-phase (biquad, `juce::dsp::IIR`) — Linear Phase y
    Phase Blend llegan en la Fase 2.
- Display central con grid, espectro FFT en tiempo real (2048 puntos, ventana
  Hann) y curva de EQ combinada.
- Handles arrastrables por banda (drag = freq/gain, rueda = Q, doble click =
  reset gain a 0 dB). Doble click en zona vacía del display crea una banda
  nueva en esa frecuencia/gain.
- Panel contextual de banda (freq/gain/Q/shape/channel/active/delete). Se
  dejó reservado el espacio de layout para el selector Full/Transient/Sustain
  y el slider Phase Blend de las Fases 2-3, para no tener que rehacer el
  panel cuando lleguen.
- Master gain de salida y save/load de presets básico (`.hybridq`, XML del
  estado del APVTS).

### Simplificaciones conscientes de esta fase (documentadas en el código)

- Low Cut / High Cut son de una sola sección biquad (12 dB/oct). Pendientes
  más pronunciadas (24/48 dB/oct vía cascada) quedan para el pulido de fases
  posteriores, no son parte del criterio de salida de Fase 1.
- Tilt se implementa como low-shelf + high-shelf en cascada con ganancias
  opuestas (técnica estándar), no como una topología de filtro dedicada.
- La curva de EQ del display suma en dB la respuesta de cada banda de forma
  independiente (no multiplica respuestas complejas) — aproximación visual
  estándar, no afecta al audio real.
- El acceso desde la UI a los coeficientes de cada `BandProcessor` (para
  dibujar la curva) no está protegido por un lock ni un double-buffer
  explícito; en el peor caso un frame de dibujo puede leer un coeficiente a
  medio actualizar (glitch visual de un frame, nunca de audio). Aceptable
  para MVP; si se quiere blindar del todo, la Fase 2 es buen momento para
  introducir un mecanismo de snapshot lock-free ya que de todos modos habrá
  que rediseñar esta parte para Phase Blend.

## Validar Fase 1

1. Todo lo de la Fase 0 (pluginval, pass-through, automatización, estado).
2. Doble click en el display crea una banda; arrastrar el handle cambia
   freq/gain en tiempo real y se refleja en la curva.
3. Cambiar el "Shape" de una banda a Low Cut / High Cut / Notch / Tilt y
   confirmar auditivamente que el comportamiento es coherente con la curva
   dibujada.
4. Cambiar "Channel" a Mid o Side en una banda y confirmar que solo afecta a
   esa parte de la imagen estéreo (prueba con una señal con contenido L/R
   diferenciado).
5. Guardar un preset, cerrar y volver a abrir el plugin en el DAW, cargar el
   preset: el estado de todas las bandas debe recuperarse exactamente.

## Siguiente paso sugerido

Fase 2 del roadmap: `LinearPhaseFilter` (FIR vía FFT partitioned
convolution), cálculo de minimum-phase equivalente (Hilbert/cepstral), y
`PhaseMorpher` para el Phase Blend continuo por banda descrito en la
sección 3.2.
