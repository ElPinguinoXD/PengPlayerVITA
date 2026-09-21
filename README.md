# PengPlayer 0.6

PengPlayer es un reproductor de musica nativo y personalizable para PlayStation Vita, desarrollado con VitaSDK, C++ y libvita2d.

## Novedades de la 0.6

La 0.6 introduce visualizadores de audio en tiempo real. El motor de reproduccion publica una copia reducida del PCM exclusivamente para visualizacion; el audio que llega a SceAudioOut no se modifica.

### Modos de visualizacion

En la pantalla **Ahora suena**, usa **Arriba / Abajo** para cambiar entre:

1. **Caratula** - vista clasica con caratula y detalles tecnicos.
2. **Espectro** - 32 bandas obtenidas mediante una FFT de 512 muestras.
3. **Onda** - waveform en tiempo real de la señal PCM.
4. **Circular** - espectro radial alrededor de la caratula.
5. **Caratula + espectro** - caratula y barras de frecuencia en una sola vista.

El modo elegido se guarda automaticamente en `ux0:data/PengPlayer/preferences.cfg` y se restaura al volver a abrir la aplicacion.

## Controles en Ahora suena

- **Arriba / Abajo:** cambiar visualizador
- **X:** pausa / continuar
- **Izquierda / Derecha:** -5 / +5 segundos
- **L / R:** cancion anterior / siguiente
- **Triangulo:** opciones de la cancion
- **Circulo:** volver
- **SELECT:** alternar entre biblioteca y Ahora suena

## Funciones acumuladas

- Biblioteca por Canciones, Artistas, Albumes y Carpetas
- Multiples rutas de musica configurables
- MP3, FLAC, WAV, OGG, OPUS y AIFF
- Reproduccion automatica de la siguiente cancion
- Metadata y caratulas embebidas
- Caratulas personalizadas por cancion
- Crop cuadrado centrado de caratulas
- Color de acento libre mediante selector HUE
- Reproduccion inmediata y estable mediante SceAudioOut BGM
- Visualizadores en tiempo real

## Compilacion

```bash
cd /d/PROYECTOS/PengPlayer
rm -rf build
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build -j4
```

El VPK resultante se genera en `build/PengPlayer.vpk`.
