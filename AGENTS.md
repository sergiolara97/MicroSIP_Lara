# AGENTS.md

Dos apps en el mismo repo: el fork **MFC** de MicroSIP 3.22.16
(`MicroSIP-3.22.16-src/`) y una reimplementación **Win32 + WebView2**
(`MicroSIP-Modern/`) que enlaza `libpjproject` sin MFC. Comentarios, docs y
commits en español. **Sin CI, sin tests, sin `opencode.json`.**

## Dónde editar

- `MicroSIP-3.22.16-src/` — código MFC. Es la única fuente; el resto es generado.
- `pjproject-2.15.1/microsip/` — copia `robocopy /MIR` que hace
  `build-microsip.ps1`. Ignorada por git: **cualquier cambio ahí se pierde**.
- `MicroSIP-Modern/host/` — app moderna: `main.cpp`, `SipCore.cpp/.h`,
  `JabraLink.cpp/.h`, `HidLink.cpp/.h`, `TeamsPresence.cpp/.h`,
  `CallToast.cpp/.h`, `web/`. El `.vcxproj` compila además `jsoncpp` desde
  `MicroSIP-3.22.16-src/lib/jsoncpp` y `sqlite3.c` desde
  `pjproject-2.15.1/third_party`.
- `integration/JabraBridge/` — puente .NET (`net472`, SDK Jabra 4.10.1).
- `dist/MicroSIP-Jabra/`, `dist/MicroSIP-Modern/` — binarios **versionados en
  git**: recompilar modifica ficheros rastreados.
- Referencias: `README.md`, `MicroSIP-Modern/README.md`,
  `docs/jabra-integration.md`.

## Comandos

```powershell
.\build-bridge.ps1    # puente -> dist\MicroSIP-Jabra\JabraBridge
.\build-microsip.ps1  # MFC + pjproject -> pjproject-2.15.1\microsip\Release\microsip.exe
.\build-modern.ps1    # host WebView2 -> dist\MicroSIP-Modern
```

- **Orden**: `build-microsip.ps1` primero (compila pjproject);
  `build-modern.ps1` aborta si falta
  `pjproject-2.15.1\lib\libpjproject-i386-Win32-vc14-Release-Static.lib`.
  Si quieres el puente dentro del paquete, ejecuta `build-bridge.ps1` antes:
  `build-modern.ps1` copia `dist\MicroSIP-Jabra\JabraBridge` **solo si ya
  existe**.
- MSBuild: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe`,
  toolset `v143`, `10.0.22621.0`, `Release|Win32`.
- pjproject debe ser **`Release-Static|Win32`** (`/MT`): `mainDlg.h` enlaza la
  lib con `#pragma comment`. El script copia `config_site.h` de
  `config_site_sample.h` (pjproject lo exige).
- `build-modern.ps1` descarga el SDK de WebView2 en `webview2-sdk/` si falta;
  `build-microsip.ps1` descarga pjproject 2.15.1, SQLiteCpp 3.3.2 y la
  amalgama de SQLite en `pjproject-2.15.1/` (ignorado por git).
- **Cierra la app antes de recompilar**: el script borra `dist\MicroSIP-Modern\`
  y falla con `Remove-Item ... siendo utilizado en otro proceso`.
- El puente solo necesita el SDK de .NET para compilar; en ejecución basta
  .NET Framework 4.7.1.

## Compilar ≠ desplegar

`build-modern.ps1` deja el paquete en `dist\`, **no** actualiza la copia que se
usa a diario. Para probar hay que copiar a `%LOCALAPPDATA%\MicroSIP-Modern\`:
`MicroSIPModern.exe` + `web\` + `ringtone.wav` (+ `JabraBridge\`). El
`microsip.ini` de ahí **no se toca** al copiar.

Lanzar con el `MicroSIP Modern.cmd` de esa carpeta (fija `/D`) o con el
directorio de trabajo puesto ahí: `microsip-modern.log` se escribe en el **cwd**,
no junto al exe. Ejecutar desde `dist\` o `Release\` crea `microsip.ini` y log
dentro del repo.

## Verificación (no hay tests)

1. Compila con los scripts que toquen.
2. **Modern**: arranca y espera `registration success, status=200` en
   `microsip-modern.log`; la X debe ocultar a la bandeja; una segunda instancia
   debe mostrar la ventana y salir; **Salir** termina de verdad.
3. **MFC**: arranca `microsip.exe`, comprueba que lanza `JabraBridge.exe`, ciérralo
   y verifica que **no queda huérfano**.
4. **Puente**: `'{"cmd":"ping"}' | .\JabraBridge.exe` →
   `{"ev":"ready",...}` + `{"ev":"pong"}`.

Para comprobar ventanas desde PowerShell, `FindWindowW` no encuentra las
ventanas propias en este equipo (devuelve 0): usa `EnumWindows` +
`GetWindowThreadProcessId` y filtra por clase (`MicroSIPModernWindow`,
`MicroSIPCallToast`) y por proceso. Las clases propias son la forma fiable de
verificar posición, visibilidad y `WS_EX_TOPMOST`.

## Gotchas de compilación (MFC)

- `mainDlg.cpp` tiene **BOM UTF-8** y caracteres no ASCII: no lo reescribas con
  herramientas que quiten el BOM. El resto de fuentes son ASCII.
- No edites `microsip.vcxproj` a mano para el build: `build-microsip.ps1` parchea
  **la copia** (quita `third_party`, añade SQLiteCpp/sqlite3, quita
  `bcg729/opus/chkstk_ms`). Si cambias el proyecto origen, replica el cambio en
  el script.
- `const.h` define `_GLOBAL_VIDEO`; el script lo comenta **solo en la copia**.
  Todo el vídeo está bajo `#ifdef _GLOBAL_VIDEO`.
- `Data\Database.cpp` enlaza SQLiteCpp/sqlite3 con `#pragma comment`; el script
  lo comenta porque esas fuentes se compilan en el proyecto.

## Interfaz MFC: tema y traducción

- `Theme.cpp/.h` centraliza la paleta; `colorMode` en `microsip.ini`
  (`0` sistema, `1` claro, `2` oscuro). `Theme::BroadcastChange()` propaga el
  cambio en caliente.
- `CThemeTabCtrl` y `StatusBar::OnPaint` son owner-draw porque los controles
  estándar no respetan colores oscuros (el tab de Windows no soporta
  `NM_CUSTOMDRAW`: se usa `TCS_OWNERDRAWFIXED`).
- Las cadenas nuevas de UI hay que añadirlas a `assets/langpack_spanish.txt`
  (`[English]` + traducción) o se quedan en inglés: MicroSIP carga el primer
  `langpack_*.txt` que encuentra junto al exe.

## App moderna: cableado

- WebView2 sirve `host/web/` como `https://microsip.local`
  (`SetVirtualHostNameToFolderMapping`). UI: `index.html` + `app.js` + `lang.js`.
- **Contrato JS↔host**: JS manda `{cmd:...}` y recibe `{ev:"snapshot", state,
  history, contacts, messages}`. La lista real de comandos está en
  `HandleMessage` (`main.cpp`); al añadir uno hay que tocar también `app.js`.
- **Identidad del llamante**: se prefiere `P-Asserted-Identity` y luego
  `Remote-Party-ID` al `From` (algunos trunks mandan
  `From: sip:trunk@dominio.ejemplo` y el número real en PAI/RPID). `BuildState`
  y el historial usan esa identidad; sin PAI/RPID se cae al `From`.
- **Caché de WebView2**: al tocar HTML/JS sube la versión en **dos** sitios o
  sirve lo viejo (UI a medias, teclado muerto): `main.cpp` (`index.html?v=N`) y
  `index.html` (`app.js?v=N`, `lang.js?v=N`).
- Mensajes: `WM_APP+1` snapshot, `+2` evento Jabra, `+3` bandeja, `+4` mostrar
  ventana, `+5` comandos IPC, `+6` botón HID (`MSIP_HID_BUTTON_MSG`),
  `+7` estado de Teams (`MSIP_TEAMS_STATE_MSG`), `+8`/`+9` contestar/colgar
  desde la notificación (`MSIP_TOAST_ANSWER`/`MSIP_TOAST_HANGUP`), `+10` abrir
  la ventana desde la notificación (`MSIP_TOAST_OPEN`).
  Timers: `1` DTMF, `3` fin de timbre, `4` pulsación larga HID, `5` estado de
  Teams (retardo de 2 s). `MSIP_TIMER_AUTOANSWER` (`2`) está definido pero no
  se usa.
- Cerrar (X/Alt+F4) **oculta a la bandeja**; salir de verdad = menú → Salir,
  bandeja → Salir o `cmd:"quit"` (`DestroyWindow`).
- `microsip.ini` propio **junto al exe** (no el de la app original), en
  **UTF-16 LE** (`GetPrivateProfileStringW`). `EnsureIniExists()` convierte un
  UTF-8 **con BOM**, pero un UTF-8 sin BOM con acentos se lee como ANSI.
  Enumera `[Account0..16]`; `[Settings] accountId` marca la activa. Si no hay
  cuentas, al arrancar importa las de `%APPDATA%\MicroSIP\microsip.ini`.
- **`Account0` es la cuenta local** (sin registro ni credenciales, para
  `sip:local@<ip>:<sourcePort>`), pero **necesita `username`**: una cuenta sin
  `username` se descarta en `ApplyAccounts()` y el INVITE se rechaza con "no
  available account".
- Para probar conferencia/transferencia sin segunda extensión: copia la app,
  `sourcePort=55539`, `rtpPortMin/Max=4100/4200`, `autoAnswer=1`,
  `singleInstance=0` y `[Account0] username=local`, y desde la principal marca
  `sip:local@127.0.0.1:55539`. Con `singleInstance=0` los argumentos **no** se
  reenvían a la instancia viva (arrancan otro proceso): para probar IPC usa
  `singleInstance=1`.
- Marcar la propia extensión también genera una llamada entrante en la misma
  instancia: sirve para probar el timbre y el aviso sin segunda línea.
- IPC externo: segunda instancia con `msip:answer|hangupall|hold|minimize|
  micmute|transfer_<num>|quit` o un número/`sip:` para marcar (también
  `/exit`, `/answer`…). Los comandos con parámetro pasan por
  `%TEMP%\microsip-modern.cmd`; el resto por `WM_APP+5`.
- **No llames a `PJ_LOG` antes de `pjsua_create()`** (la importación de cuentas
  ocurre antes y el proceso peta). `LoadSettings()` antes de `pjsua_init()`;
  `sourcePort` se lee antes de crear el transporte.
- `password=` de MicroSIP va cifrado con clave privada no publicada: la
  importación lo descarta y marca `needsPassword`. No intentes descifrarlo.
- `User-Agent: MicroSIP/3.22.22` es deliberado (el de la app instalada).
- **Micrófono solo en llamada**: `snd_auto_close_time=1` y sin
  `pjsua_set_snd_dev` al arrancar. El `pjmedia_tonegen` del DTMF se conecta solo
  mientras suena (`PollDtmfTone`); si se deja conectado el micro no se cierra.
- Timbre: `assets/ringtone.wav` con `waveOut` (bucle), salida `ringDevice` y
  `volumeRing`. No abre el micrófono.
- Ventana inicial **420x664** (cliente): el dialer está calculado para ese alto.
- El `<style>` de `index.html` termina con un bloque que anula el andamiaje del
  prototipo (`.stage*` → `display:none`); va al final a propósito.
- Ajustes `[Settings]`: `autoAnswer`, `callWaiting`, `forwarding`(`1`/`all`),
  `forwardingNumber`, `featureCodeCP`, `audioCodecs`, `forceCodec`,
  `DTMFMethod`(`rfc2833`/`sipinfo`), `volumeRing`, `ringDevice`, `language`,
  `localDTMF`, `accentColor`, `alwaysOnTop`, `hidHeadset`, `hidHeadsetIds`,
  `hidHoldMs`, `teamsPresence`, `callToast`, `callToastPosition`,
  `callToastScreen`.
- `pjsua_media_config_default` ya trae `clock_rate`: no lo pongas a 0 (rompe el
  conference bridge).

## i18n (app moderna)

`host/web/lang.js` traduce en sitio con un diccionario **ES → EN/CA/PT/FR/DE**
(claves normalizadas sin acentos). `language` vacío = idioma del sistema
(`navigator.language`; el host usa `GetUserDefaultUILanguage` para la bandeja).
Cambiar el selector de Ajustes → General guarda `language` y **recarga** la
WebView. El menú de bandeja se traduce en C++ con `HostText`.

Se llama `lang.js` y **no** `i18n.js` porque Bitdefender bloquea la creación de
cualquier fichero con ese nombre (falso positivo): en `%LOCALAPPDATA%` el
`Copy-Item` falla con `Acceso denegado` y el recurso queda en 404. No lo
renombres de vuelta.

## Botón de auriculares USB (`HidLink`)

Cubre auriculares USB que **no** son Jabra y que no exponen la página de
telefonía `0x0B`. Los EarPods USB-C (`05AC:110B`) mandan el botón central como
**Play/Pause** de la página de consumo (`0x0C`, usage `0xCD`).

- Corta con timbre → contesta; corta en llamada → mute; larga (`hidHoldMs`,
  1200 ms) → rechaza / cuelga. Fuera de llamada se ignora (Windows sigue
  recibiendo la tecla: la lectura HID no es exclusiva).
- **El hilo debe registrarse con `pj_thread_register` antes de `PJ_LOG`**: si no,
  `pj_thread_check_stack` desreferencia un `pj_thread_t` nulo y el proceso muere
  con `0xC0000005` leyendo `+0x38`.
- Descarta teclados/ratones: las colecciones de teclado acaban en `\kbd` y no se
  pueden abrir, así que se detectan por la ruta y se excluye todo su `VID:PID`
  (si no, las teclas multimedia contestarían llamadas).
- Reportes de los EarPods: 3 bytes, `01 01 00` pulsado / `01 00 00` suelto.
  `HidP_GetUsages` quiere los datos **sin** Report ID: se prueban los
  desplazamientos 0 y 1 y vale el que dé `HIDP_STATUS_SUCCESS`. Asumir Report ID
  cuando no lo hay da `HIDP_STATUS_INVALID_REPORT_LENGTH` (`0xC0110003`) y el
  botón "no hace nada".

## Notificación de llamada (`CallToast`)

Ventana propia con el número/contacto y dos botones, **solo mientras entra la
llamada**: al contestar o al colgar desaparece. Ajustes `callToast` (`1`),
`callToastPosition` (`topright` por defecto; `topleft`, `bottomright`,
`bottomleft`) y `callToastScreen` (`primary` por defecto = pantalla principal;
`app` = pantalla donde está MicroSIP).

- **No es un toast de WinRT**: los botones de un toast de una app sin empaquetar
  necesitan un servidor COM (`INotificationActivationCallback`) con AUMID,
  acceso directo en el menú Inicio y registro en
  `HKCU\Software\Classes\CLSID\{...}\LocalServer32`. Aquí no hay instalador, así
  que la tarjeta se dibuja a mano con GDI (redondeada, Segoe UI, color de acento
  de la app).
- **Solo aparece si la app no está en primer plano**: se considera "delante"
  cuando `IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd) &&
  GetForegroundWindow() == g_hwnd`; en ese caso no se muestra (si ya se ve la
  pantalla de llamada, sobra). Se re-evalúa en cada snapshot y en
  `WM_ACTIVATE`, `WM_SHOWWINDOW` y `WM_SIZE`.
- `WS_EX_TOPMOST` (se reafirma en cada `Sync`) para que quede por encima de
  cualquier ventana normal, y `WS_EX_NOACTIVATE` para **no robar el foco** al
  pulsar Contestar/Colgar.
- Botones → mensajes a la ventana principal: `MSIP_TOAST_ANSWER` (`+8`) y
  `MSIP_TOAST_HANGUP` (`+9`) con el id de llamada en `wParam`;
  `MSIP_TOAST_OPEN` (`+10`) al pulsar el cuerpo (trae la app al frente).
- **Sin ventana propietaria a propósito**: una ventana hija de la principal se
  ocultaría con ella cuando la app está en la bandeja.
- Se coloca en la esquina del **área de trabajo** (`MONITORINFO.rcWork`) del
  monitor de la ventana principal, escalada con `GetDpiForWindow` (el manifiesto
  es PerMonitorV2).
- `SipCore::ContactNameFor()` resuelve el nombre de la agenda comparando los
  últimos 9 dígitos (el PBX manda a veces prefijos distintos).
- Verificado con llamadas reales: app delante → no aparece; minimizada → aparece
  en la esquina configurada; al volver al frente desaparece; Contestar conecta y
  la quita; Colgar rechaza.

## Estado de Microsoft Teams (`TeamsPresence`)

`SipCore::UpdateTeamsPresence()` pone Teams en **Ocupado** mientras hay una
llamada `CONFIRMED` y **al colgar restaura el estado que hubiera** antes (ajuste
`teamsPresence`, por defecto `1`).

- **No hay API local de presencia.** La vía oficial es Microsoft Graph
  (`POST /me/presence/setPresence`, permiso delegado `Presence.ReadWrite`), pero
  requiere consentimiento de administrador. Aquí no se usa.
- Se usan los argumentos que trae el propio cliente MSIX
  (`%LOCALAPPDATA%\Microsoft\WindowsApps\ms-teams.exe`):
  `--set-presence-to-busy`, `--set-presence-to-available`, `--set-presence-to-dnd`,
  `--set-presence-to-away`, `--set-presence-to-offline`,
  `--set-presence-to-be-right-back`, `--reset-presence`. Aplican el estado con la
  sesión ya iniciada del usuario: sin Graph ni permisos.
- **Dos `ms-teams.exe` seguidos se pisan y el segundo se pierde** (una llamada de
  1 s se quedaba en Ocupado). Por eso hay margen de 2 s y se aplica solo el
  estado final: `MSIP_TIMER_TEAMS` (timer `5`) + `teamsPending_`.
- **Al colgar se restaura el estado que había**, no siempre Disponible: al entrar
  en llamada se lee el nombre accesible del botón de perfil (`"Tu perfil, estado
  No molestar"`) y se traduce al argumento correspondiente; si era automático
  (en reunión, en llamada, presentando) se usa `--reset-presence` y Teams lo
  recalcula. La lectura usa UI Automation y corre en el hilo de la ventana
  (`MSIP_TEAMS_STATE_MSG`), no en el de pjsua.
- **`FindWindowW(L"TeamsWebView")` devuelve otra ventana** de Teams (hay varias
  con la misma clase): hay que enumerar con `EnumWindows` y probar las que
  tienen título hasta encontrar la que expone el botón de perfil.
- Si Teams no está abierto **no se lanza** (el comando lo arrancaría): se omite.
  Al cerrar la app en mitad de una llamada se restaura el estado guardado en
  `Shutdown()`.
- Para leer el estado real desde fuera: el botón de perfil de Teams expone
  `AutomationId=idna-me-control-avatar-trigger` y su `Name` es
  *"Tu perfil, estado Disponible"* (UIA). Útil para verificar sin tocar nada.

## Jabra (app moderna)

`JabraLink.cpp` ⇄ JSON por stdin/stdout ⇄ `JabraBridge.exe`. El estado que se
manda al auricular se calcula en `SendSnapshot()`.

- **No mandes `offhook=true` mientras la llamada solo timbra**: el SDK lo toma
  como llamada activa, devuelve `HookSwitch` y el host lo interpreta como
  "contestar" → la llamada se autodescolgaba. Para timbre:
  `offhook=false, ring=true`. Es el mismo criterio que la app MFC
  (`GetCallsCount()` excluye INCOMING/EARLY por defecto).
- Bajar el boom para contestar llega como `HookSwitch` (y en algunos modelos
  como `PhoneMute`); ambos contestan si hay una entrante.
- `PhoneMute` es también el acuse de cada `Mute()` que manda el puente. Si se
  reenvía, el host lo trata como una pulsación y el mute oscila en bucle: el
  puente descarta el `PhoneMute` que llega menos de 2 s después de un `Mute()`.
- `microsip-modern.log` lo escribe pjsua **sin `flush`**: durante una prueba en
  vivo puede ir con retraso; al cerrar la app se vuelca.
- Para probar una entrante real sin segunda extensión, manda un `INVITE` UDP al
  puerto efímero del proceso (`Get-NetUDPEndpoint`). Marcar la propia extensión
  crea además una pata saliente y el auricular puede mandar `PhoneMute` en vez
  de `HookSwitch`.

## Jabra (app MFC)

`MicroSIP (Jabra.cpp)` ⇄ JSON por stdin/stdout ⇄ `JabraBridge.exe` ⇄ SDK.
`Jabra::Start()` en `PJCreateRaw()`, `Stop()` en `PJDestroy()`; eventos por
`UM_JABRA_EVENT` → `CmainDlg::onJabraEvent`. Sin Jabra con call control se
reabre el HID genérico (`lib/Hid.cpp`). Ajustes: `jabraIntegration` (1),
`jabraBoomHangup`. Si cambias el protocolo, actualiza `Program.cs`, `Jabra.cpp`
y `docs/jabra-integration.md`.

## Entorno y despliegue

- **Si falta un recurso de `web/` el síntoma es "abre pero no se puede pulsar
  nada"**, no un error visible: `app.js` revienta en su primera línea que use el
  recurso y no llega a registrar ningún `addEventListener`. Comprueba la consola
  de la WebView antes de buscar el fallo en C++.
- **Bitdefender** bloquea el exe por archivo **y por red**: hay que permitirlo en
  Firewall, no basta con excluir carpetas. Síntomas: UDP con `408` silencioso,
  TCP con `WSAEACCES (10013)`. Exclusiones: la carpeta del repo,
  `%APPDATA%\MicroSIP`, `%LOCALAPPDATA%\MicroSIP`,
  `%LOCALAPPDATA%\MicroSIP-Modern` y el proceso `MicroSIPModern.exe`. Si el AV
  borra algo, hay una copia de seguridad local; **no** cambies
  los nombres canónicos como workaround.
- No tengas la app original y la moderna abiertas con la misma extensión: el
  registro de la original puede seguir vivo hasta 300 s (`Expires`).
- Códigos de registro: `200` = OK, `401` = contraseña, `408` = el PBX no
  contesta (red/firewall/puerto).
- Artefactos de ejecución (ignorados por `.gitignore`, no commitear):
  `microsip.ini`, `microsip-modern.log`, `history.tsv`, `messages.tsv`,
  `webview-data/`, `recordings/`, `call_log.db`, `contacts.db`, `Contacts.xml`.

## Límites del binario

- Equivalente a **MicroSIP-Lite**: sin vídeo ni Opus/G.729 (`third_party` no se
  publica). Codecs: G.711, G.722, GSM, iLBC, Speex, L16.
- `HidLink` solo entiende el botón de consumo `0xCD`. Auriculares con página de
  telefonía `0x0B` (Poly, algunos Logitech) siguen sin call control.
