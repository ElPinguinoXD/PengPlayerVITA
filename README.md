# PengPlayer 0.1

Primer prototipo nativo para PS Vita.

## Incluido ahora

- UI nativa 960x544 con libvita2d.
- Ruta visible de la biblioteca.
- Inicio en `ux0:/music`.
- Navegacion por carpetas.
- Carpetas primero y orden A-Z / Z-A.
- Filtrado de extensiones de audio:
  MP3, FLAC, WAV, OGG, OPUS, M4A, AAC, AIFF/AIF, WMA y AC3.
- Seleccion de una cancion.
- Mini-player inferior provisional mostrando nombre y ruta.

Esta build todavia NO decodifica ni reproduce audio. El siguiente modulo sera el motor de reproduccion.

## Dependencia

```bash
vdpm install libvita2d
```

## Compilar

```bash
cd ~/PengPlayer
rm -rf build
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build -j4
```

Resultado:

```text
build/PengPlayer.vpk
```

## Controles

- D-Pad arriba/abajo: mover seleccion
- X: entrar a carpeta / seleccionar audio
- O: volver atras
- Triangulo: A-Z / Z-A
- START: salir

## Prueba recomendada

Crea algo similar en la Vita:

```text
ux0:/music/
  PengX/
    test.flac
    prueba.mp3
  Soundtracks/
    track.wav
```

Al seleccionar un archivo debe aparecer en el mini-player inferior junto con su ruta completa.
