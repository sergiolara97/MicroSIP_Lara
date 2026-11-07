# MicroSIP Modern

Versión de MicroSIP con interfaz moderna (WebView2 + HTML/CSS/JS) sobre el
motor **pjsua** del proyecto original.

## Ejecutar

```
dist\MicroSIP-Modern\MicroSIPModern.exe
```

No requiere instalación: el ejecutable es estático (/MT) y WebView2 viene con
Windows 10/11. El puente Jabra (`JabraBridge\`) se lanza automáticamente si
hay auricular.

### De dónde salen las cuentas

La app usa su propio `microsip.ini` (junto al exe). Si todavía no tiene
cuentas, al arrancar **importa automáticamente** las de MicroSIP desde:

1. `%APPDATA%\MicroSIP\microsip.ini` (instalación normal).
2. `%LOCALAPPDATA%\MicroSIP\microsip.ini`.
3. `..\MicroSIP-Jabra\microsip.ini` (copia portable junto a la app).

En **Ajustes → Cuenta SIP** se muestra la ruta del archivo en uso y el origen
de la importación. El botón **Importar cuentas de MicroSIP** vuelve a copiar
las cuentas (útil si cambias usuario o contraseña en la app original). El
botón **+** crea una cuenta nueva (`AccountN` en `microsip.ini`).

## Qué incluye

| Área | Estado |
| --- | --- |
| Registro SIP (UDP) desde `microsip.ini` (`[AccountN]`) | ✅ |
| **Multicuenta**: enumeración de `AccountN`, selector y alta desde la UI | ✅ |
| Llamadas: marcar, contestar, colgar, retener | ✅ |
| DTMF en llamada (teclado) | ✅ |
| Transferencia (REFER) | ✅ |
| Grabación a WAV en `recordings\` | ✅ |
| **Mensajería SIP** (MESSAGE saliente/entrante y vista de chat) | ✅ |
| Audio: volumen de salida/micrófono, mute | ✅ |
| Tono DTMF local al marcar (como la app original) | ✅ |
| Historial persistente (`history.tsv`) | ✅ |
| Contactos desde `contacts.db` (SQLite) | ✅ |
| No molestar / grabación (estado en la UI) | ✅ |
| Tema sistema/claro/oscuro con **acento configurable** (rojo/verde/azul) | ✅ |
| Logo en cabecera e icono propio de la aplicación | ✅ |
| Integración Jabra (SDK oficial vía `JabraBridge.exe`) | ✅ |
| Siempre visible (always on top) desde el menú | ✅ |
| Atajos de marcación rápida (importados de MicroSIP) | ✅ |
| Cerrar a la bandeja del sistema (X oculta; menú Abrir/Salir) | ✅ |
| Presencia (estado en línea de contactos) | ⏳ pendiente |
| Vídeo | ❌ no disponible en este build |

### Por qué no hay vídeo

Este binario enlaza un pjproject compilado sin `third_party` (FFmpeg/VPX), que
MicroSIP no publica. Es equivalente a **MicroSIP-Lite**: G.711, G.722, GSM,
iLBC, Speex y L16, sin vídeo ni Opus/G.729. Añadirlo exige compilar antes esas
librerías y rehacer el enlace; no es una limitación de la interfaz.

## Estructura

```
MicroSIP-Modern/
  host/
    main.cpp           Win32 + WebView2 + puente JS
    SipCore.h/.cpp     motor pjsua (cuentas, llamadas, mensajes, historial)
    JabraLink.h/.cpp   integración con JabraBridge.exe
    web/               UI (index.html + app.js)
    MicroSIPModern.vcxproj
  prototype/           prototipo visual original (referencia)
```

## Puente JS ↔ C++

- C++ → JS: `PostWebMessageAsJson({ ev: "snapshot", state, history,
  contacts, messages })`
- JS → C++: `{ cmd: "dial" | "answer" | "hangup" | "hold" | "mute" | "dtmf" |
  "transfer" | "volume" | "dnd" | "record" | "theme" | "saveAccount" |
  "setAccount" | "sendMessage" }`

`state.accounts` contiene todas las cuentas con su estado de registro y
`state.activeAccount` marca la activa. Los mensajes se guardan en
`messages.tsv` y se envían por la cuenta activa.

## Compilar

```powershell
.\build-modern.ps1
```

Requiere pjproject compilado (`.\build-microsip.ps1`) y descarga el SDK de
WebView2 si falta.

## Limitaciones

- Un solo transporte UDP (el puerto se puede fijar con `[Settings] sourcePort`).
- Sin presencia ni suscripción de buddies.
- Sin vídeo (ver arriba).
- El auricular Jabra está integrado, pero los eventos físicos dependen del
  dispositivo conectado.

## Verificación realizada

- Llamada real entre MicroSIP Modern y MicroSIP (cuenta local, UDP 5060) de 2:29.
- Grabación generada en `recordings\call-YYYYMMDD-HHMMSS.wav` (WAV 16 kHz, ~4,7 MB).
- DTMF enviado desde el teclado de la llamada.
- Transferencia enviada (REFER) y estado "En espera" en la UI.
- Historial persistido en `history.tsv` y visible en Registros.
- Multicuenta: `accounts loaded: 2`, dos `acc_add ... status=0` y cambio de
  cuenta activa desde el selector (`active account switched to Account2`).
- Mensajería bidireccional entre dos instancias (puertos 5070/5071):
  `im_send ... status=0` e `im_received ... peer=...` en ambos sentidos.
