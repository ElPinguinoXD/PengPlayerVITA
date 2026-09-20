# PengPlayer 0.4

PengPlayer es un reproductor de musica nativo y personalizable para PlayStation Vita, desarrollado en C++ con VitaSDK.

## Novedades de la version 0.4

Esta version introduce una biblioteca musical persistente. PengPlayer deja de depender unicamente del explorador de carpetas y organiza la musica usando los metadatos reales de cada archivo.

### Biblioteca

La pantalla principal ahora tiene cuatro secciones:

- **Canciones**: todas las pistas encontradas, ordenadas por titulo.
- **Artistas**: agrupa las canciones por artista.
- **Albumes**: agrupa las canciones por album.
- **Carpetas**: agrupa las canciones segun la carpeta donde estan almacenadas y muestra su ruta.

Pulsa `△` para cambiar de seccion.

### Indice persistente

La biblioteca se guarda en:

```text
ux0:data/PengPlayer/library.dat
```

Por eso PengPlayer no tiene que releer todos los metadatos cada vez que se abre. El primer escaneo procesa las canciones de forma progresiva para mantener la interfaz activa y, al terminar, guarda el indice.

### Rutas de musica configurables

Desde:

```text
START > Ajustes > Rutas de musica
```

puedes añadir carpetas desde:

```text
ux0:/
uma0:/
imc0:/
```

La configuracion se guarda en:

```text
ux0:data/PengPlayer/roots.txt
```

PengPlayer puede escanear varias rutas al mismo tiempo. Esto permite usar, por ejemplo, `ux0:/music/` y otra carpeta de una memoria secundaria.

En el selector de carpetas:

- `X`: entra en una carpeta.
- `△`: usa la carpeta actual como ruta de musica.
- `O`: vuelve atras.

En la pantalla de rutas, `□` elimina una ruta configurada. PengPlayer siempre exige que quede al menos una ruta.

### Actualizar la biblioteca

Si copias, eliminas o modificas musica fuera de PengPlayer, usa:

```text
START > Ajustes > Actualizar biblioteca
```

El escaneo ocurre mientras la interfaz sigue funcionando y muestra su progreso.

### Reproduccion

Se mantiene todo lo implementado en 0.2 y 0.3:

- MP3, FLAC, WAV, OGG, Opus y AIFF.
- Inicio de reproduccion practicamente inmediato.
- Audio estable sin clicks ni pops.
- Play / pausa.
- Seek de -5/+5 segundos.
- Cancion anterior y siguiente.
- Reproduccion automatica de la siguiente pista al terminar.
- Cola basada en la vista desde la que se inicio la reproduccion.
- Metadatos de titulo, artista, album, genero y ano.
- Caratulas embebidas en MP3 y FLAC.
- Caratulas `cover.jpg`, `folder.jpg`, `front.jpg` y `album.jpg`.
- Pantalla **Ahora suena**.

## Controles principales

| Boton | Accion |
| --- | --- |
| `↑ / ↓` | Navegar |
| `X` | Abrir / reproducir |
| `△` | Cambiar seccion de biblioteca |
| `□` | Pausa / continuar |
| `← / →` | -5 / +5 segundos |
| `L / R` | Cancion anterior / siguiente |
| `SELECT` | Abrir / cerrar Ahora suena |
| `START` | Ajustes |
| `O` | Volver |

## Compilacion

```bash
cd /d/PROYECTOS/PengPlayer
rm -rf build
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build -j4
```

El VPK se genera en:

```text
build/PengPlayer.vpk
```

## Proximos objetivos

- Caratula personalizada por cancion elegida desde la propia app.
- Selector de color/acento de la interfaz.
- Playlists y categorias personalizadas.
- Modos repetir una, repetir todo y aleatorio.
- Visualizadores de espectro y waveform.
- Mejoras de busqueda y ordenamiento.
- Reproduccion avanzada en segundo plano y futura integracion con juegos.
