# Avisos de terceros

Este proyecto incluye codigo y binarios de terceros. Cada componente conserva
su licencia original; este archivo solo los enumera y atribuye.

## MicroSIP (aplicacion MFC)

- MicroSIP 3.22.16 — <https://www.microsip.org>
- Copyright (C) 2011-2026 MicroSIP
- Licencia: **GPL-2.0-or-later** (texto en [`LICENSE`](LICENSE))

## pjproject (pjsip)

- pjproject 2.15.1 — <https://www.pjsip.org>
- Copyright (C) 2008-2011 Teluu Inc.; Copyright (C) 2003-2008 Benny Prijono
- Licencia: **GPL-2.0-or-later** o licencia comercial de Teluu

pjproject incorpora, entre otros, estos componentes:

| Componente | Licencia |
| --- | --- |
| `third_party/resample` | LGPL-2.1 ([`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt)) |
| `third_party/speex` | BSD-3-Clause |
| `third_party/gsm` | TU Berlin (permisiva) |
| `third_party/ilbc` | BSD-3-Clause |
| `third_party/webrtc`, `webrtc_aec3` | BSD-3-Clause |
| `third_party/srtp` | BSD-3-Clause |
| `third_party/yuv` | BSD-3-Clause |
| `third_party/BaseClasses` | BSD-3-Clause (muestras de DirectShow) |
| `third_party/sqlite3` | Dominio publico |
| `third_party/SQLiteCpp` | MIT ([`LICENSES/MIT.txt`](LICENSES/MIT.txt)) |

El texto completo de cada licencia esta en el arbol de pjproject que descarga
`build-microsip.ps1` (version 2.15.1, etiqueta oficial).

## Jabra .NET SDK

- `Jabra.NET.Sdk` 4.10.1, `Jabra.NET.Sdk.Properties`,
  `Jabra.NET.Sdk.GnProtocol`, `Jabra.DeviceConnector.Windows` y
  `Jabra.Properties.Definition`
- Copyright (c) GN Audio A/S
- Licencia: **Apache-2.0** ([`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt))
- Se redistribuyen sin modificaciones como parte de `JabraBridge\`.

## Microsoft WebView2

- WebView2 SDK — Copyright (c) Microsoft Corporation
- Licencia: **BSD-3-Clause** ([`LICENSES/WebView2.txt`](LICENSES/WebView2.txt))
- El runtime de WebView2 **no** se incluye: lo instala Windows 10/11 o el
  propio usuario mediante el instalador de Microsoft.

## SQLite

- `sqlite3` — Dominio publico — <https://sqlite.org/copyright.html>

## SQLiteCpp

- Copyright (c) 2012-2024 Sebastien Rombauts
- Licencia: **MIT** ([`LICENSES/MIT.txt`](LICENSES/MIT.txt))

## Feather Icons

- Icono "phone" usado en `MicroSIP-Modern/host/web/logo.svg`
- Copyright (c) 2013-present Cole Bemis
- Licencia: **MIT** ([`LICENSES/MIT.txt`](LICENSES/MIT.txt))

## Paquetes .NET

`System.Text.Json`, `Microsoft.Bcl.AsyncInterfaces`, `System.Reactive`,
`JsonSchema.Net`, `Json.More`, `JsonPointer.Net`, `Humanizer`,
`NuGet.Versioning`, `System.Memory`, `System.Buffers` y similares se
distribuyen bajo **MIT** o **Apache-2.0**. Las licencias completas acompanan a
cada paquete en NuGet.

## Marcas

"MicroSIP" y "Jabra" son marcas de sus respectivos titulares. Se usan en este
repositorio unicamente para describir el origen del codigo y la compatibilidad
con los dispositivos. Este proyecto no esta afiliado ni respaldado por
MicroSIP ni por GN Audio A/S.
