# MicroSIP + Jabra

Softphone de escritorio para Windows basado en **MicroSIP 3.22.16**, con
integración de auriculares **Jabra** y una reimplementación moderna con
interfaz WebView2.

> **Proyecto no oficial.** No está afiliado, patrocinado ni respaldado por
> MicroSIP ni por GN Audio A/S. "MicroSIP" y "Jabra" son marcas de sus
> respectivos titulares y se usan aquí solo para describir el origen del código
> y la compatibilidad con los dispositivos.

## Dos aplicaciones

| | `MicroSIP-Jabra` | `MicroSIP-Modern` |
| --- | --- | --- |
| Base | Fork de MicroSIP 3.22.16 (C++ / MFC) | Win32 + WebView2, sin MFC |
| Interfaz | MFC clásica, tema claro/oscuro, acento rojo | HTML/CSS/JS, 6 idiomas |
| Motor SIP | pjsua (pjproject 2.15.1) | pjsua (pjproject 2.15.1) |
| Jabra | SDK oficial vía `JabraBridge.exe` | igual |
| Auriculares USB no Jabra | HID genérico | `HidLink` (botón de EarPods USB-C) |

## Qué incluye

**App MFC**

- Botón de colgar/descolgar del auricular: descolgar, colgar, contestar y
  rechazar llamadas.
- LED de llamada y de timbre, mute sincronizado y *busylight* durante la
  llamada.
- Opción de colgar al plegar el boom (desactivada por defecto).
- Detección en caliente de conexión/desconexión del auricular.
- Sin Jabra, se usa el soporte HID genérico de siempre.
- Tema claro/oscuro/sistema, acento rojo y traducción al español.

**App moderna**

- Registro SIP (UDP), multicuenta y selector de cuenta activa.
- Llamadas: marcar, contestar, colgar, retener, transferir (REFER),
  transferencia atendida y conferencia.
- DTMF en llamada, grabación a WAV e historial persistente.
- Mensajería SIP (MESSAGE) con vista de chat.
- Contactos desde `contacts.db`, presencia, buzón (MWI) y "no molestar".
- Notificación de llamada entrante con botones Contestar/Colgar.
- Estado de Microsoft Teams sincronizado (Ocupado durante la llamada).
- Botón de auriculares USB (EarPods) para contestar, silenciar y colgar.
- Cerrar a la bandeja del sistema, "siempre visible" y atajos de marcación.
- Interfaz en ES / EN / CA / PT / FR / DE y acento configurable.

## Descargar y ejecutar

El paquete ya compilado está en:

```
dist\MicroSIP-Jabra\    microsip.exe + JabraBridge\ + langpack_spanish.txt
dist\MicroSIP-Modern\   MicroSIPModern.exe + web\ + JabraBridge\
```

Copia la carpeta que quieras donde prefieras y ejecuta el `.exe`. No hace falta
instalar nada: los ejecutables son estáticos (`/MT`) y el puente Jabra solo
necesita .NET Framework 4.7.1 (incluido en Windows 10/11).

- La app MFC usa `microsip.ini` junto al ejecutable.
- La app moderna usa su propio `microsip.ini` junto al ejecutable. Si no tiene
  cuentas, las importa de `%APPDATA%\MicroSIP\microsip.ini`,
  `%LOCALAPPDATA%\MicroSIP\microsip.ini` o `..\MicroSIP-Jabra\microsip.ini`.
- La contraseña guardada por MicroSIP está cifrada con una clave privada que no
  se publica: la importación la descarta y la app pide escribirla una vez.

## Compilar

Requisitos: **Visual Studio 2022 Build Tools** con el toolset C++ y MFC.

```powershell
.\build-microsip.ps1  # pjproject + app MFC  -> pjproject-2.15.1\microsip\Release\microsip.exe
.\build-bridge.ps1    # puente Jabra         -> dist\MicroSIP-Jabra\JabraBridge
.\build-modern.ps1    # app moderna         -> dist\MicroSIP-Modern
```

Orden recomendado: `build-microsip.ps1` primero (compila pjproject),
`build-bridge.ps1` después y `build-modern.ps1` al final (copia el puente).

Los scripts descargan pjproject 2.15.1, SQLiteCpp 3.3.2, la amalgama de SQLite
y el SDK de WebView2 desde sus fuentes oficiales.

> Este build es equivalente a **MicroSIP-Lite**: sin vídeo y sin Opus/G.729
> (dependían de `third_party`, que MicroSIP no publica). Códecs disponibles:
> G.711, G.722, GSM, iLBC, Speex y L16.

## Uso rápido

```ini
[Settings]
jabraIntegration=1
jabraBoomHangup=0
colorMode=0
```

- `jabraIntegration=0` fuerza el modo HID clásico.
- `colorMode`: `0` sistema, `1` claro, `2` oscuro.
- En la app moderna, Ajustes se divide en Apariencia, Cuenta, Audio, Red,
  General y Llamadas.

## Estructura

```
MicroSIP-3.22.16-src/     Código de MicroSIP (C++ / MFC)
  Jabra.cpp / Jabra.h     Puente con JabraBridge.exe
integration/JabraBridge/  Puente .NET (net472) sobre el SDK de Jabra
MicroSIP-Modern/          App Win32 + WebView2 (SipCore, JabraLink, HidLink)
  host/web/               UI (index.html, app.js, lang.js)
dist/                     Paquetes listos para ejecutar
assets/                   langpack_spanish.txt y ringtone.wav
docs/                     Documentación de la integración Jabra
LICENSES/                 Textos de licencias de terceros
```

## Licencia

- El código de este repositorio se distribuye bajo **GNU GPL v2 o posterior**
  (`GPL-2.0-or-later`). Texto completo en [`LICENSE`](LICENSE).
- El puente `integration/JabraBridge/` se distribuye bajo **Apache-2.0**
  ([`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt)), por compatibilidad
  con el SDK de Jabra.
- Los componentes de terceros y sus licencias están en
  [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

MicroSIP original: © 2011-2026 MicroSIP, GPL-2.0-or-later.
pjproject: © Teluu Inc. y Benny Prijono, GPL-2.0-or-later.

Al redistribuir los binarios debes conservar estos avisos y ofrecer el código
fuente correspondiente (este repositorio).

## Privacidad

La aplicación funciona en local: no incluye telemetría ni envía datos a
terceros. Las cuentas SIP, el historial, los contactos, los mensajes y las
grabaciones se guardan en el equipo del usuario. El tráfico SIP/RTP va
directamente al servidor PBX configurado.

## Créditos

- [MicroSIP](https://www.microsip.org) — softphone original.
- [pjproject](https://www.pjsip.org) — pila SIP y multimedia.
- [Jabra Developer Zone](https://developer.jabra.com) — SDK de auriculares.
- [Feather Icons](https://feathericons.com) — icono del logo (MIT).
