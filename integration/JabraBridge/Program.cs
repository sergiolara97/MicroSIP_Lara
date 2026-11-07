/*
 * JabraBridge - Puente entre MicroSIP y el SDK oficial de Jabra (.NET SDK 4.x)
 *
 * Este proceso se lanza desde MicroSIP y habla JSON por stdin/stdout.
 *
 *  MicroSIP  ->  bridge (stdin):
 *      {"cmd":"state","offhook":true,"ring":false,"mute":false,"hold":false}
 *      {"cmd":"busylight","on":true}
 *      {"cmd":"ping"}
 *      {"cmd":"quit"}
 *
 *  bridge -> MicroSIP (stdout):
 *      {"ev":"ready","sdk":"4.10.1"}
 *      {"ev":"scan","devices":1}
 *      {"ev":"device","connected":true,"name":"Jabra Evolve2 65","serial":"...","productId":1234,"callControl":true}
 *      {"ev":"device","connected":false,"name":"...","serial":"..."}
 *      {"ev":"signal","name":"HookSwitch","value":true,"change":"Absolute"}
 *      {"ev":"signal","name":"PhoneMute","value":true,"change":"Absolute"}
 *      {"ev":"signal","name":"Redial","value":true,"change":"Relative"}
 *      {"ev":"signal","name":"Flash","value":true,"change":"Relative"}
 *      {"ev":"signal","name":"RejectCall","value":true,"change":"Relative"}
 *      {"ev":"busylight","supported":true}
 *      {"ev":"error","message":"..."}
 */

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Jabra.NET.Sdk.Core;
using Jabra.NET.Sdk.Core.Types;
using Jabra.NET.Sdk.Modules.CallControl;
using Jabra.NET.Sdk.Modules.CallControl.Signals;
using Jabra.NET.Sdk.Properties;

namespace JabraBridge
{
    internal static class Program
    {
        private static JabraService _service;

        private static async Task<int> Main(string[] args)
        {
            var options = BridgeOptions.Parse(args);

            // stdout es el canal de eventos: debe ser UTF-8 sin BOM y sin buffering.
            var stdout = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false))
            {
                AutoFlush = true
            };
            var stderr = new StreamWriter(Console.OpenStandardError(), new UTF8Encoding(false))
            {
                AutoFlush = true
            };

            _service = new JabraService(
                emit: line => { lock (stdout) { stdout.WriteLine(line); } },
                log: line => { lock (stderr) { stderr.WriteLine(line); } });

            try
            {
                await _service.StartAsync(options);
            }
            catch (Exception ex)
            {
                _service.Emit("{\"ev\":\"error\",\"message\":" + JsonSerializer.Serialize(ex.Message) + "}");
                return 2;
            }

            // Bucle de comandos. Al cerrarse stdin (MicroSIP terminó) salimos.
            string line;
            while ((line = await Console.In.ReadLineAsync()) != null)
            {
                if (string.IsNullOrWhiteSpace(line))
                {
                    continue;
                }

                try
                {
                    if (!HandleCommand(line))
                    {
                        break;
                    }
                }
                catch (Exception ex)
                {
                    _service.Emit("{\"ev\":\"error\",\"message\":" + JsonSerializer.Serialize(ex.Message) + "}");
                }
            }

            await _service.DisposeAsync();
            return 0;
        }

        /// <summary>Devuelve false cuando hay que terminar.</summary>
        private static bool HandleCommand(string line)
        {
            using (var doc = JsonDocument.Parse(line))
            {
                JsonElement root = doc.RootElement;
                if (root.ValueKind != JsonValueKind.Object)
                {
                    return true;
                }

                string cmd = GetString(root, "cmd");
                switch (cmd)
                {
                    case "quit":
                        return false;

                    case "ping":
                        _service.Emit("{\"ev\":\"pong\"}");
                        return true;

                    case "state":
                        _service.ApplyState(
                            offhook: GetBool(root, "offhook", false),
                            ring: GetBool(root, "ring", false),
                            mute: GetBool(root, "mute", false),
                            hold: GetBool(root, "hold", false));
                        return true;

                    case "busylight":
                        _service.SetBusylight(GetBool(root, "on", false));
                        return true;

                    default:
                        return true;
                }
            }
        }

        private static string GetString(JsonElement root, string name)
        {
            return root.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
                ? value.GetString()
                : null;
        }

        private static bool GetBool(JsonElement root, string name, bool fallback)
        {
            if (!root.TryGetProperty(name, out JsonElement value))
            {
                return fallback;
            }

            switch (value.ValueKind)
            {
                case JsonValueKind.True:
                    return true;
                case JsonValueKind.False:
                    return false;
                case JsonValueKind.Number:
                    return value.GetInt32() != 0;
                case JsonValueKind.String:
                    return value.GetString() == "1" || string.Equals(value.GetString(), "true", StringComparison.OrdinalIgnoreCase);
                default:
                    return fallback;
            }
        }
    }

    internal sealed class BridgeOptions
    {
        public string PartnerKey = "";
        public string AppId = "microsip";
        public string AppName = "MicroSIP";

        public static BridgeOptions Parse(string[] args)
        {
            var options = new BridgeOptions();
            foreach (string arg in args)
            {
                if (arg.StartsWith("--partner-key=", StringComparison.OrdinalIgnoreCase))
                {
                    options.PartnerKey = arg.Substring("--partner-key=".Length);
                }
                else if (arg.StartsWith("--app-id=", StringComparison.OrdinalIgnoreCase))
                {
                    options.AppId = arg.Substring("--app-id=".Length);
                }
                else if (arg.StartsWith("--app-name=", StringComparison.OrdinalIgnoreCase))
                {
                    options.AppName = arg.Substring("--app-name=".Length);
                }
            }

            string envKey = Environment.GetEnvironmentVariable("JABRA_PARTNER_KEY");
            if (!string.IsNullOrEmpty(envKey) && string.IsNullOrEmpty(options.PartnerKey))
            {
                options.PartnerKey = envKey;
            }

            return options;
        }
    }

    /// <summary>
    /// Envoltorio del SDK de Jabra: descubrimiento de dispositivos, call control
    /// (hook, mute, ring, hold) y busylight.
    /// </summary>
    internal sealed class JabraService : IAsyncDisposable
    {
        private static readonly string[] BusylightProperties = { "busyStatePresence" };

        private readonly Action<string> _emitLine;
        private readonly Action<string> _log;
        private readonly object _stateLock = new();
        private readonly System.Collections.Concurrent.ConcurrentBag<IDisposable> _subscriptions = new();

        private IManualApi _api;
        private CallControlFactory _callControlFactory;
        private IPropertyModule _propertyModule;
        private IPropertyFactory _propertyFactory;

        private IDevice _device;
        private ICallControl _callControl;
        private IPropertyMap _propertyMap;
        private bool _busylightSupported;

        // Último estado pedido por MicroSIP. Se reaplica al conectar un dispositivo.
        private bool? _offhook;
        private bool? _ring;
        private bool? _mute;
        private bool? _hold;
        private bool _busylightOn;
        private bool _busylightKnown;

        public JabraService(Action<string> emit, Action<string> log)
        {
            _emitLine = emit;
            _log = log;
        }

        public void Emit(string jsonLine)
        {
            _emitLine(jsonLine);
        }

        public async Task StartAsync(BridgeOptions options)
        {
            var config = new Config(
                partnerKey: options.PartnerKey ?? "",
                appId: string.IsNullOrEmpty(options.AppId) ? "microsip" : options.AppId,
                appName: string.IsNullOrEmpty(options.AppName) ? "MicroSIP" : options.AppName);

            _api = Init.InitManualSdk(config);
            _callControlFactory = new CallControlFactory(_api);
            _propertyModule = new PropertyModule(_api);

            _subscriptions.Add(_api.LogEvents.Subscribe(logEvent =>
            {
                if (logEvent.Level == LogLevel.Error)
                {
                    _log("[jabra] " + logEvent);
                }
            }));

            _subscriptions.Add(_api.DeviceAdded.Subscribe(device => _ = Task.Run(() => OnDeviceAddedAsync(device))));
            _subscriptions.Add(_api.DeviceRemoved.Subscribe(device => OnDeviceRemoved(device)));
            _subscriptions.Add(_api.InitialScanComplete.Subscribe(_ =>
            {
                // Se informa solo de los dispositivos que pueden controlar llamadas:
                // es lo que MicroSIP usa para decidir si mantiene activo el modo Jabra
                // o vuelve al soporte HID generico.
                int compatible = 0;
                var devices = _api.CurrentDevices;
                if (devices != null)
                {
                    foreach (IDevice device in devices)
                    {
                        try
                        {
                            if (_callControlFactory.SupportsCallControl(device))
                            {
                                compatible++;
                            }
                        }
                        catch
                        {
                            // dispositivo no consultable: se ignora
                        }
                    }
                }

                Emit("{\"ev\":\"scan\",\"devices\":" + compatible + "}");
            }));

            await _api.Start();
            Emit("{\"ev\":\"ready\",\"sdk\":\"" + _api.GetSdkVersion() + "\"}");
        }

        private async Task OnDeviceAddedAsync(IDevice device)
        {
            try
            {
                bool supportsCallControl = _callControlFactory.SupportsCallControl(device);
                Emit("{\"ev\":\"device\",\"connected\":true,\"name\":" + Json(device.Name) +
                     ",\"serial\":" + Json(device.SerialNumber) +
                     ",\"productId\":" + device.ProductId +
                     ",\"callControl\":" + (supportsCallControl ? "true" : "false") + "}");

                if (!supportsCallControl)
                {
                    _log("[jabra] " + device.Name + " no soporta call control");
                    return;
                }

                // Solo un dispositivo controla las llamadas a la vez.
                lock (_stateLock)
                {
                    if (_device != null)
                    {
                        _log("[jabra] Ya hay un dispositivo activo, se ignora " + device.Name);
                        return;
                    }

                    _device = device;
                }

                ICallControl callControl = await _callControlFactory.CreateCallControl(device);
                if (callControl == null)
                {
                    lock (_stateLock) { _device = null; }
                    return;
                }

                try
                {
                    bool locked = await callControl.TakeCallLock();
                    _log("[jabra] Call lock " + (locked ? "adquirido" : "no disponible") + " para " + device.Name);
                }
                catch (Exception ex)
                {
                    _log("[jabra] TakeCallLock falló: " + ex.Message);
                }

                lock (_stateLock)
                {
                    _callControl = callControl;
                }

                _subscriptions.Add(callControl.DeviceSignals.Subscribe(OnSignal));
                _subscriptions.Add(callControl.OnDisconnect.Subscribe(_ => OnCallControlDisconnected(device)));

                await SetupBusylightAsync(device);

                // Reaplicar el estado real de MicroSIP al dispositivo recién conectado.
                ReapplyState();
            }
            catch (Exception ex)
            {
                Emit("{\"ev\":\"error\",\"message\":" + Json("device: " + ex.Message) + "}");
            }
        }

        private void OnDeviceRemoved(IDevice device)
        {
            bool wasActive;
            lock (_stateLock)
            {
                wasActive = _device != null && _device.SerialNumber == device.SerialNumber;
                if (wasActive)
                {
                    _device = null;
                    _callControl = null;
                    _propertyMap = null;
                    _busylightSupported = false;
                }
            }

            // Solo el dispositivo activo afecta a MicroSIP.
            if (!wasActive)
            {
                return;
            }

            Emit("{\"ev\":\"device\",\"connected\":false,\"name\":" + Json(device.Name) +
                 ",\"serial\":" + Json(device.SerialNumber) + "}");

            _log("[jabra] Dispositivo activo desconectado: " + device.Name);
        }

        private void OnCallControlDisconnected(IDevice device)
        {
            lock (_stateLock)
            {
                if (_device != null && _device.SerialNumber == device.SerialNumber)
                {
                    _callControl = null;
                }
            }

            _log("[jabra] Conexión de call control perdida: " + device.Name);
        }

        private void OnSignal(ICallControlSignal signal)
        {
            if (signal == null)
            {
                return;
            }

            string change = signal.ValueChangeType == ValueChangeType.Absolute ? "Absolute" : "Relative";
            Emit("{\"ev\":\"signal\",\"name\":" + Json(signal.Type.ToString()) +
                 ",\"value\":" + (signal.Value ? "true" : "false") +
                 ",\"change\":\"" + change + "\"}");
        }

        private async Task SetupBusylightAsync(IDevice device)
        {
            try
            {
                _propertyFactory = _propertyFactory ?? await _propertyModule.CreatePropertyFactory();
                IPropertyMap map = await _propertyFactory.CreateProperties(device, BusylightProperties);
                if (map == null)
                {
                    return;
                }

                lock (_stateLock)
                {
                    _propertyMap = map;
                    _busylightSupported = true;
                }

                Emit("{\"ev\":\"busylight\",\"supported\":true}");

                if (_busylightKnown)
                {
                    await WriteBusylightAsync(_busylightOn);
                }
            }
            catch (Exception ex)
            {
                // No todos los dispositivos soportan busylight; no es un error fatal.
                _log("[jabra] busylight no disponible en " + device.Name + ": " + ex.Message);
                Emit("{\"ev\":\"busylight\",\"supported\":false}");
            }
        }

        public void ApplyState(bool offhook, bool ring, bool mute, bool hold)
        {
            ICallControl callControl;
            lock (_stateLock)
            {
                callControl = _callControl;
                _offhook = offhook;
                _ring = ring;
                _mute = mute;
                _hold = hold;
            }

            if (callControl == null)
            {
                return;
            }

            try
            {
                callControl.OffHook(offhook);
                callControl.Ring(ring);
                callControl.Mute(mute);
                callControl.Hold(hold);
            }
            catch (Exception ex)
            {
                _log("[jabra] ApplyState falló: " + ex.Message);
            }
        }

        public void SetBusylight(bool on)
        {
            lock (_stateLock)
            {
                _busylightOn = on;
                _busylightKnown = true;
                if (!_busylightSupported)
                {
                    return;
                }
            }

            _ = Task.Run(async () =>
            {
                try
                {
                    await WriteBusylightAsync(on);
                }
                catch (Exception ex)
                {
                    _log("[jabra] busylight: " + ex.Message);
                }
            });
        }

        private async Task WriteBusylightAsync(bool on)
        {
            IPropertyMap map;
            lock (_stateLock)
            {
                map = _propertyMap;
            }

            if (map == null)
            {
                return;
            }

            IPropertyTransaction transaction = map.StartTransaction();
            transaction.Set("busyStatePresence", PropertyValue.FromBoolean(on));
            await transaction.Commit();
        }

        private void ReapplyState()
        {
            bool? offhook;
            bool? ring;
            bool? mute;
            bool? hold;
            lock (_stateLock)
            {
                offhook = _offhook;
                ring = _ring;
                mute = _mute;
                hold = _hold;
            }

            if (offhook.HasValue && ring.HasValue && mute.HasValue && hold.HasValue)
            {
                ApplyState(offhook.Value, ring.Value, mute.Value, hold.Value);
            }
        }

        public async ValueTask DisposeAsync()
        {
            ICallControl callControl;
            lock (_stateLock)
            {
                callControl = _callControl;
                _callControl = null;
                _device = null;
            }

            if (callControl != null)
            {
                try
                {
                    await callControl.ReleaseCallLock();
                }
                catch (Exception ex)
                {
                    _log("[jabra] ReleaseCallLock: " + ex.Message);
                }
            }

            foreach (IDisposable subscription in _subscriptions)
            {
                try
                {
                    subscription.Dispose();
                }
                catch
                {
                    // ignorado a propósito: cerrando
                }
            }

            // ConcurrentBag no tiene Clear(); se descarta junto con el proceso.
        }

        private static string Json(string value)
        {
            return JsonSerializer.Serialize(value ?? "");
        }
    }
}
