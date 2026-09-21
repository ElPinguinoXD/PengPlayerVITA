# PengPlayer 0.9

Reproductor de música nativo y personalizable para PlayStation Vita, desarrollado con VitaSDK.

## Novedades de 0.9

### Reproducción en segundo plano y LiveArea

PengPlayer 0.9 mantiene la reproducción al salir al LiveArea mediante integración con AppMgr, el puerto BGM y una configuración de aplicación compatible con segundo plano.

En `START -> Ajustes -> Reproducción` se incluyen:

- `Segundo plano (LiveArea)`: permite mantener el audio al salir de PengPlayer.
- `Evitar suspensión mientras suena`: evita la suspensión automática completa durante la reproducción.
- `Apagar pantalla ahora`: apaga el display mientras la música continúa.

La reproducción continúa avanzando en LiveArea y el cambio automático a la siguiente canción sigue funcionando fuera de la aplicación.

### Acceso correcto a la biblioteca de música

PengPlayer inicializa AppUtil antes de acceder a `ux0:/music`, monta explícitamente el almacenamiento mediante `sceAppUtilMusicMount()` y lo desmonta limpiamente al salir. Esto evita errores de acceso como `System error: Not owner` después de reiniciar la consola.

### Puerto BGM

Cuando el segundo plano está activado, PengPlayer mantiene la propiedad del puerto BGM durante la sesión para evitar conflictos al pausar, reanudar o restaurar una sesión. La pantalla de Reproducción muestra el estado del BGM y el código de error cuando una operación falla.

### Integración de sistema

El `PARAM.SFO` utiliza `ATTRIBUTE=17338376` (`0x01089008`) para permitir la ejecución necesaria durante el uso de LiveArea.

## Funciones heredadas

PengPlayer 0.9 conserva todo lo implementado hasta 0.8:

- MP3, FLAC, WAV, OGG, Opus y AIFF.
- Reproducción inmediata y sin clicks.
- Metadata y carátulas.
- Carátulas personalizadas con crop cuadrado centrado.
- Biblioteca por canciones, artistas, álbumes y carpetas.
- Múltiples rutas de música.
- Playlists con nombres personalizados.
- Cola editable, shuffle y repetición.
- Favoritos y categorías.
- Búsqueda.
- Recientes y más reproducidas.
- Restauración de sesión.
- Visualizadores de espectro, onda y circular.
- Selector HUE y escala de interfaz.
- Confirmaciones antes de eliminaciones persistentes.

## Alcance

La 0.9 se centra en LiveArea, pantalla apagada y reproducción estable en segundo plano. La reproducción simultánea dentro de juegos queda reservada para una versión posterior.

## Compilación

```bash
rm -rf build
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build -j4
```

El VPK se genera en:

`build/PengPlayer.vpk`
