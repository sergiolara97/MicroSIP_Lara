# Integración de auriculares Jabra en MicroSIP

Integración basada en el **SDK oficial de Jabra (.NET SDK 4.x)**, tal y como lo
documenta <https://developer.jabra.com/sdks-and-tools/dotnet/>.

MicroSIP es una aplicación C++/MFC, así que el SDK (que es .NET) se ejecuta en un
proceso auxiliar, `JabraBridge.exe`, que habla JSON con MicroSIP por
`stdin`/`stdout`. El ejecutable principal no gana dependencias: si el puente no
está presente, MicroSIP arranca exactamente igual que siempre.

```
+----------------+        JSON (stdin/stdout)        +-------------------+
|  MicroSIP.exe  | <-------------------------------> |  JabraBridge.exe  |
|   C++ / MFC    |                                   |  .NET + Jabra SDK |
+----------------+                                   +-------------------+
                                                              |
                                                     Jabra Device Connector
                                                              |
                                                       Auricular Jabra
```

## Qué se integra

| Función | Estado | Origen en el SDK |
| --- | --- | --- |
| Detección de auriculares (conectar / desconectar en caliente) | Implementado | `IApi.DeviceAdded/DeviceRemoved` |
| Botón de colgar/descolgar: descolgar, colgar, contestar, rechazar | Implementado | `ICallControl.DeviceSignals` (`HookSwitch`, `RejectCall`) |
| LED de llamada en curso / timbre en el auricular | Implementado | `ICallControl.OffHook()`, `Ring()` |
| Silencio (mute) sincronizado en ambos sentidos | Implementado | `ICallControl.Mute()`, señal `PhoneMute` |
| Plegar el micrófono (boom) cuelga la llamada | Implementado (opcional) | señal `PhoneMute` + ajuste `jabraBoomHangup` |
| Busylight (luz de ocupado) encendida durante la llamada | Implementado | propiedad `busyStatePresence` |
| Botón de rellamada (redial) | Implementado | señal `Redial` |
| Llamada en espera: el botón rechaza la segunda llamada | Implementado | `RejectCall` / `HookSwitch` |
| Hold / resume desde el auricular | Parcial (el estado de hold se envía al dispositivo; el botón de hold no está mapeado a MicroSIP) | `ICallControl.Hold()` |
| Swap entre dos llamadas | No implementado (MicroSIP no tiene esa acción expuesta) | señal `Flash` |
| Teclado numérico del auricular | No implementado | señales `PhoneKey0..9` |
| Ajustes y telemetría del dispositivo (ANC, sidetone, batería, etc.) | No implementado | módulo `Properties` |
| Personalización de botones/LED | No implementado | módulo `ButtonCustomization` |
| Emparejamiento Bluetooth de dongles Link 380/390 | No implementado | módulo `DevicePairing` |
| Actualización de firmware | No implementado | `JabraCLI` |

El SDK ofrece además telemetría (batería, nivel de ruido, calidad de enlace) y
`JabraCLI` para gestión masiva; se pueden añadir más adelante sobre la misma
arquitectura sin tocar MicroSIP.

## Instalación

1. Compila MicroSIP (ver más abajo) y copia `JabraBridge.exe` **con toda su
   carpeta** junto a `MicroSIP.exe`:

   ```
   MicroSIP.exe
   JabraBridge\
       JabraBridge.exe
       JabraBridge.exe.config
       Jabra.NET.Sdk.dll
       ...
       Jabra.Utilities\
   ```

   MicroSIP busca el puente en este orden:
   `.\JabraBridge\JabraBridge.exe`, `.\JabraBridge.exe`, `.\jabra\JabraBridge.exe`.

2. Arranca MicroSIP. La integración se activa sola (`jabraIntegration=1` por
   defecto). Si no hay ningún auricular Jabra, MicroSIP vuelve al soporte HID
   genérico automáticamente.

3. Requisitos del equipo: Windows 10/11 y **.NET Framework 4.7.1** (viene
   instalado de serie). No hace falta instalar Jabra Direct ni ningún runtime
   .NET moderno.

## Ajustes (`microsip.ini`, sección `[Settings]`)

| Clave | Por defecto | Descripción |
| --- | --- | --- |
| `jabraIntegration` | `1` | Activa/desactiva el puente Jabra. `0` fuerza el modo HID clásico. |
| `jabraBoomHangup` | `0` | `1` = al silenciar el micrófono (plegar el boom) se cuelga la llamada en curso. |
| `headsetSupport` | `0` | Soporte HID genérico. Se usa solo si Jabra no está activo. |

Ejemplo:

```ini
[Settings]
jabraIntegration=1
jabraBoomHangup=1
headsetSupport=1
```

## Partner Key (recomendado para producción)

El SDK funciona sin clave, pero Jabra recomienda una **Partner Key** para
garantizar la coexistencia con otros softphones (Teams, Zoom, Jabra Direct...).
Se solicita en <https://developer.jabra.com/support/>.

Se puede pasar al puente de tres formas:

1. Variable de entorno `JABRA_PARTNER_KEY`.
2. Argumento `--partner-key=XXXX` (MicroSIP arranca el puente sin argumentos; se
   puede añadir en `Jabra::Start()` si se necesita).
3. Modificando `JabraBridge.exe.config` (no recomendado).

Sin clave el SDK escribe un aviso en el log y sigue funcionando.

## Compilar el puente

```powershell
dotnet publish integration\JabraBridge\JabraBridge.csproj `
    -c Release -f net472 -o dist\MicroSIP-Jabra\JabraBridge
```

O con el script incluido:

```powershell
.\build-bridge.ps1
```

## Compilar MicroSIP

```powershell
.\build-microsip.ps1
```

El script descarga y compila pjproject 2.15.1, copia el código a
`pjproject-2.15.1\microsip`, adapta el proyecto y genera
`pjproject-2.15.1\microsip\Release\microsip.exe`.

Ese build es **equivalente a MicroSIP-Lite**: sin vídeo y con los codecs de
pjproject (G.711, G.722, GSM, iLBC, Speex, L16). Las librerías `third_party` que
MicroSIP no publica (ffmpeg, vpx, SDL2, x264, openssl, silk, ipp, opus, bcg729,
amr) quedan fuera. La integración Jabra no depende de ninguna de ellas.

## Diagnóstico

- El puente escribe los errores por `stderr` (no se muestran en la UI).
- MicroSIP registra los eventos en su log habitual (`PJ_LOG`) cuando el log está
  activado: busca las líneas `Jabra.cpp`.
- Para volcar el protocolo entre el SDK y el conector de Jabra, crea el fichero
  `%LOCALAPPDATA%\MicroSIP-Modern\jabra-debug.on`: el puente escribe
  `jabra-transport.log` en esa carpeta. Bórralo para dejar de generar el volcado.
- Para probar el puente a mano:

  ```powershell
  cd dist\JabraBridge
  '{"cmd":"ping"}' | .\JabraBridge.exe
  ```

  Debe responder `{"ev":"ready","sdk":"4.10.1"}` y `{"ev":"pong"}`.

## Protocolo (para extensiones)

Eventos del puente a MicroSIP:

```json
{"ev":"ready","sdk":"4.10.1"}
{"ev":"scan","devices":1}
{"ev":"device","connected":true,"name":"Jabra Evolve2 65","serial":"...","productId":1234,"callControl":true}
{"ev":"device","connected":false,"name":"...","serial":"..."}
{"ev":"signal","name":"HookSwitch","value":true,"change":"Absolute"}
{"ev":"signal","name":"PhoneMute","value":true,"change":"Absolute"}
{"ev":"signal","name":"Redial","value":true,"change":"Relative"}
{"ev":"busylight","supported":true}
{"ev":"error","message":"..."}
```

Comandos de MicroSIP al puente:

```json
{"cmd":"state","offhook":true,"ring":false,"mute":false,"hold":false}
{"cmd":"state","offhook":true,"ring":false,"mute":true,"hold":false,"muteFromDevice":true}
{"cmd":"busylight","on":true}
{"cmd":"ping"}
{"cmd":"quit"}
```

`muteFromDevice` (opcional) lo pone MicroSIP cuando el cambio de mute lo originó
el propio auricular (boom o botón). El puente **aplica igualmente** el `Mute()`
al dispositivo, también en ese caso: así su micrófono real y su LED siguen a la
app y no quedan desfasados (el dispositivo solo informa de pulsaciones, no de
estado). Para que el acuse de ese `Mute()` no se confunda con una pulsación, el
puente descarta el primer `PhoneMute` que llega en los 250 ms siguientes, y
cualquier repetición a menos de 300 ms de la anterior (antirrebote).
