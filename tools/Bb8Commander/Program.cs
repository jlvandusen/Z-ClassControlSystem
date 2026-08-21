// ============================================================
//  BB8 Commander — build / upload / monitor for the BB-8 fleet
//  Wraps arduino-cli (same toolchain as the Arduino IDE) and adds
//  a full serial monitor: pinned input line with history, live
//  telemetry status bar, multi-board view, CSV logging.
//
//  bb8 list                         targets + detected ports
//  bb8 build  <target|all>          compile
//  bb8 upload <target> [--port COMx]   compile-if-needed + flash
//  bb8 deploy <target> [--port COMx]   build + upload + monitor
//  bb8 monitor <targets...|COMx> [--baud n] [--log file.csv] [--raw] [--show-tlm]
//  bb8 identify                     probe ESP32 ports, read boot banner
// ============================================================

using System.Diagnostics;
using System.IO.Ports;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

var configPath = FindConfig();
if (configPath is null)
{
    Fail("targets.json not found (looked in current dir and C:\\Users\\james\\BB8).");
    return 1;
}
var config = JsonSerializer.Deserialize<Bb8Config>(File.ReadAllText(configPath), JsonCtx.Default.Bb8Config)!;

if (args.Length == 0) { PrintHelp(); return 0; }

try
{
    switch (args[0].ToLowerInvariant())
    {
        case "list":     return CmdList();
        case "build":    return CmdBuild(Arg(1));
        case "upload":   return await CmdUpload(Arg(1), Opt("--port"));
        case "deploy":
        {
            var t = Arg(1);
            var port = Opt("--port");
            var rc = await CmdUpload(t, port);
            if (rc != 0) return rc;
            Console.WriteLine();
            return await CmdMonitor(PositionalArgs(1), port);
        }
        case "monitor":  return await CmdMonitor(PositionalArgs(1), Opt("--port"));
        case "analyze":  return CmdAnalyze(Arg(1));
        case "identify": return await CmdIdentify();
        case "help": case "-h": case "--help": PrintHelp(); return 0;
        default:
            Fail($"Unknown command '{args[0]}'."); PrintHelp(); return 1;
    }
}
catch (Exception ex)
{
    Fail(ex.Message);
    return 1;
}

// ------------------------------------------------------------------
string? Arg(int i) => args.Length > i && !args[i].StartsWith("--") ? args[i] : null;

List<string> PositionalArgs(int from)
{
    var list = new List<string>();
    for (int i = from; i < args.Length; i++)
    {
        if (args[i].StartsWith("--")) { i++; continue; }  // skip option + value
        list.Add(args[i]);
    }
    return list;
}

string? Opt(string name)
{
    for (int i = 0; i < args.Length - 1; i++)
        if (string.Equals(args[i], name, StringComparison.OrdinalIgnoreCase))
            return args[i + 1];
    return null;
}

bool Flag(string name) => args.Any(a => string.Equals(a, name, StringComparison.OrdinalIgnoreCase));

string? FindConfig()
{
    foreach (var dir in new[] { Directory.GetCurrentDirectory(), @"C:\Users\james\BB8" })
    {
        var p = Path.Combine(dir, "targets.json");
        if (File.Exists(p)) return p;
        var parent = Directory.GetParent(dir);
        while (parent is not null)
        {
            var pp = Path.Combine(parent.FullName, "targets.json");
            if (File.Exists(pp)) return pp;
            parent = parent.Parent;
        }
    }
    return null;
}

void PrintHelp()
{
    Console.WriteLine("""
        BB8 Commander — build / upload / monitor the BB-8 boards (wraps arduino-cli)

          bb8 list                                targets + detected serial ports
          bb8 build <target|all>                  compile (drive|dome|body|imu)
          bb8 upload <target> [--port COMx]       compile + flash
          bb8 deploy <target> [--port COMx]       build + upload + monitor
          bb8 monitor <targets...|COMx> [--baud n] [--log file.csv] [--raw] [--show-tlm]
          bb8 analyze <file.csv>                  tuning analysis of a logged session
          bb8 identify                            probe ports, read boot banners

        Monitor (full-screen):
          - type + Enter          send a serial command to the ACTIVE board
                                  (help / telemetry on / pid show / debug s2s ...)
          - Up / Down             command history
          - Tab                   switch active board (when monitoring several)
          - Ctrl+C  or  Esc       exit
          - telemetry lines render in the live status bar instead of scrolling
            (--show-tlm scrolls them too; --log always captures everything)
          - --raw = plain line streaming, no UI (for piping/CI)

        Examples:
          bb8 monitor drive
          bb8 monitor drive body --log session.csv
          bb8 monitor COM7 --baud 115200
        """);
}

Bb8Target ResolveTarget(string? name)
{
    if (name is null) throw new ArgumentException("Target name required (drive|dome|body|imu).");
    var t = config.Targets.FirstOrDefault(t => t.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
    return t ?? throw new ArgumentException($"Unknown target '{name}'. Known: {string.Join(", ", config.Targets.Select(x => x.Name))}");
}

// ------------------------------------------------------------------
int CmdList()
{
    Console.WriteLine("Targets:");
    foreach (var t in config.Targets)
        Console.WriteLine($"  {t.Name,-6} {t.Sketch,-22} {t.Fqbn,-42} {t.Description}");

    Console.WriteLine();
    Console.WriteLine("Serial ports:");
    var ports = DetectPorts();
    if (ports.Count == 0) Console.WriteLine("  (none detected — plug boards in via USB)");
    foreach (var p in ports)
    {
        var guess = GuessTargets(p);
        Console.WriteLine($"  {p.Port,-8} VID={p.Vid ?? "?"} PID={p.Pid ?? "?"}  {(guess.Count > 0 ? "-> " + string.Join(" or ", guess) : "")}");
    }
    return 0;
}

List<string> GuessTargets(PortInfo p) =>
    config.Targets
        .Where(t => t.UsbVid is not null &&
                    string.Equals(t.UsbVid, p.Vid, StringComparison.OrdinalIgnoreCase) &&
                    string.Equals(t.UsbPid, p.Pid, StringComparison.OrdinalIgnoreCase))
        .Select(t => t.Name).ToList();

List<PortInfo> DetectPorts()
{
    var (rc, stdout, _) = Run(config.ArduinoCli, "board list --json", capture: true);
    var result = new List<PortInfo>();
    if (rc == 0 && stdout.Length > 0)
    {
        try
        {
            using var doc = JsonDocument.Parse(stdout);
            if (doc.RootElement.TryGetProperty("detected_ports", out var arr))
            {
                foreach (var item in arr.EnumerateArray())
                {
                    if (!item.TryGetProperty("port", out var port)) continue;
                    var address = port.GetProperty("address").GetString() ?? "";
                    var proto = port.TryGetProperty("protocol", out var pr) ? pr.GetString() : "";
                    if (proto != "serial") continue;
                    string? vid = null, pid = null;
                    if (port.TryGetProperty("properties", out var props))
                    {
                        if (props.TryGetProperty("vid", out var v)) vid = v.GetString()?.Replace("0x", "");
                        if (props.TryGetProperty("pid", out var p2)) pid = p2.GetString()?.Replace("0x", "");
                    }
                    result.Add(new PortInfo(address, vid, pid));
                }
            }
        }
        catch (JsonException) { }
    }
    if (result.Count == 0)
        foreach (var name in SerialPort.GetPortNames().Distinct())
            result.Add(new PortInfo(name, null, null));
    return result;
}

int CmdBuild(string? name)
{
    var targets = name is null or "all"
        ? config.Targets
        : new List<Bb8Target> { ResolveTarget(name) };

    foreach (var t in targets)
    {
        var sketch = Path.Combine(config.SketchRoot, t.Sketch);
        var buildPath = Path.Combine(config.BuildRoot, t.Sketch);
        Console.WriteLine($"\u001b[36m[BUILD] {t.Name} ({t.Sketch}) — {t.Fqbn}\u001b[0m");
        var (rc, _, _) = Run(config.ArduinoCli,
            $"compile --fqbn {t.Fqbn} --build-path \"{buildPath}\" \"{sketch}\"", capture: false);
        if (rc != 0) { Fail($"Build failed for {t.Name}."); return rc; }
    }
    Console.WriteLine("\u001b[32m[BUILD] OK\u001b[0m");
    return 0;
}

async Task<int> CmdUpload(string? name, string? port)
{
    var t = ResolveTarget(name);
    var rc = CmdBuild(t.Name);
    if (rc != 0) return rc;

    port ??= await AutoPort(t);
    if (port is null)
    {
        Fail($"No port found for '{t.Name}'. Plug the board in, or pass --port COMx (bb8 list shows candidates).");
        return 1;
    }

    var sketch = Path.Combine(config.SketchRoot, t.Sketch);
    var buildPath = Path.Combine(config.BuildRoot, t.Sketch);

    // Caterina/SAMD boards race their ~8 s bootloader window (the bootloader
    // can even enumerate on a different COM number) - attempt twice.
    bool nativeUsbBoot = t.Fqbn.Contains(":avr:") || t.Fqbn.Contains(":samd:");
    int attempts = nativeUsbBoot ? 2 : 1;
    int urc = 1;
    for (int a = 1; a <= attempts; a++)
    {
        Console.WriteLine($"\u001b[36m[UPLOAD] {t.Name} -> {port}{(a > 1 ? " (retry)" : "")}\u001b[0m");
        (urc, _, _) = Run(config.ArduinoCli,
            $"upload -p {port} --fqbn {t.Fqbn} --input-dir \"{buildPath}\" \"{sketch}\"", capture: false);
        if (urc == 0) break;
        if (a < attempts)
        {
            Console.WriteLine("\u001b[33m[UPLOAD] Bootloader race - waiting 3 s and retrying...\u001b[0m");
            await Task.Delay(3000);
        }
    }
    if (urc != 0)
    {
        Fail($"Upload failed for {t.Name}. (If a monitor holds this port, close it first.)");
        if (nativeUsbBoot)
        {
            Console.WriteLine("[HINT] Manual bootloader entry for 32u4/Trinket:");
            Console.WriteLine("  1. Double-tap the board reset button (LED pulses = bootloader, ~8 s window)");
            Console.WriteLine("  2. bb8 list - the bootloader may appear on a DIFFERENT COM number");
            Console.WriteLine("  3. bb8 upload <target> --port COMx with that number, quickly");
        }
        return urc;
    }
    Console.WriteLine($"\u001b[32m[UPLOAD] {t.Name} flashed on {port}\u001b[0m");
    return 0;
}

async Task<string?> AutoPort(Bb8Target t)
{
    var candidates = DetectPorts().Where(p => GuessTargets(p).Contains(t.Name)).ToList();
    if (candidates.Count == 1) return candidates[0].Port;
    if (candidates.Count == 0) return null;

    Console.WriteLine($"[INFO] {candidates.Count} candidate ports for '{t.Name}' — probing boot banners...");
    foreach (var c in candidates)
    {
        var banner = await ReadBootBanner(c.Port, t.Baud, 4000);
        if (banner is not null && t.BannerMatch is not null &&
            banner.Contains(t.BannerMatch, StringComparison.OrdinalIgnoreCase))
        {
            Console.WriteLine($"[INFO] {c.Port} identified as '{t.Name}'");
            return c.Port;
        }
    }
    Fail($"Could not identify which of [{string.Join(", ", candidates.Select(c => c.Port))}] is '{t.Name}'. Pass --port COMx.");
    return null;
}

async Task<int> CmdIdentify()
{
    var ports = DetectPorts();
    if (ports.Count == 0) { Fail("No serial ports detected."); return 1; }
    foreach (var p in ports)
    {
        Console.Write($"{p.Port,-8} VID={p.Vid ?? "?"} PID={p.Pid ?? "?"}  ");
        var banner = await ReadBootBanner(p.Port, 115200, 4000);
        if (banner is null) { Console.WriteLine("(no banner — board silent or busy)"); continue; }
        var match = config.Targets.FirstOrDefault(t =>
            t.BannerMatch is not null && banner.Contains(t.BannerMatch, StringComparison.OrdinalIgnoreCase));
        Console.WriteLine(match is not null
            ? $"-> \u001b[32m{match.Name}\u001b[0m ({FirstLine(banner)})"
            : $"-> unknown ({FirstLine(banner)})");
    }
    return 0;

    static string FirstLine(string s)
    {
        var line = s.Split('\n').Select(l => l.Trim()).FirstOrDefault(l => l.Length > 3) ?? "";
        return line.Length > 60 ? line[..60] : line;
    }
}

async Task<string?> ReadBootBanner(string portName, int baud, int timeoutMs)
{
    try
    {
        using var sp = new SerialPort(portName, baud) { ReadTimeout = 250, DtrEnable = false, RtsEnable = false };
        sp.Open();
        sp.DtrEnable = true; sp.RtsEnable = true;   // reset pulse (ESP32 auto-reset; harmless on CDC boards)
        await Task.Delay(100);
        var sb = new StringBuilder();
        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < timeoutMs)
        {
            try { sb.Append(sp.ReadExisting()); } catch (TimeoutException) { }
            if (sb.Length > 4000) break;
            await Task.Delay(50);
        }
        return sb.Length > 0 ? sb.ToString() : null;
    }
    catch (Exception)
    {
        return null;
    }
}

// ------------------------------------------------------------------
//  MONITOR
// ------------------------------------------------------------------
async Task<int> CmdMonitor(List<string> names, string? portOpt)
{
    int baud = int.TryParse(Opt("--baud"), out var b) ? b : 115200;
    var logFile = Opt("--log");
    bool raw = Flag("--raw") || Console.IsOutputRedirected;
    bool showTlm = Flag("--show-tlm");

    if (names.Count == 0) { Fail("monitor: give one or more targets (drive|dome|body|imu) or a COM port."); return 1; }

    // Resolve every name to (label, port, baud)
    var channels = new List<Channel>();
    var colors = new[] { "\u001b[33m", "\u001b[36m", "\u001b[32m", "\u001b[35m" }; // yellow cyan green magenta
    foreach (var (name, i) in names.Select((n, i) => (n, i)))
    {
        string label, port;
        int chBaud = baud;
        if (name.StartsWith("COM", StringComparison.OrdinalIgnoreCase))
        {
            label = name.ToUpperInvariant();
            port = label;
        }
        else
        {
            var t = ResolveTarget(name);
            chBaud = t.Baud;
            var p = (names.Count == 1 ? portOpt : null) ?? await AutoPort(t);
            if (p is null) { Fail($"No port for '{t.Name}'. Plug it in or pass --port."); return 1; }
            label = t.Name;
            port = p;
        }
        channels.Add(new Channel(label, port, chBaud, colors[i % colors.Length]));
    }

    StreamWriter? log = null;
    if (logFile is not null)
    {
        log = new StreamWriter(logFile, append: false) { AutoFlush = true };
        log.WriteLine("time,board,line");
    }

    foreach (var ch in channels) ch.TryOpen();

    var session = new MonitorSession(channels, log, raw, showTlm);
    var rc2 = session.Run();
    log?.Dispose();
    foreach (var ch in channels) ch.Close();
    return rc2;
}

// ------------------------------------------------------------------
//  ANALYZE — tuning analysis of a logged monitor session
// ------------------------------------------------------------------
int CmdAnalyze(string? file)
{
    if (file is null || !File.Exists(file)) { Fail("analyze: give a CSV logged with 'bb8 monitor ... --log file.csv'."); return 1; }

    var samples = new List<Dictionary<string, double>>();
    var events = new List<string>();
    foreach (var rawLine in File.ReadLines(file).Skip(1))
    {
        // format: HH:mm:ss.fff,board,line  (line may be CSV-quoted)
        var c1 = rawLine.IndexOf(',');
        if (c1 < 0) continue;
        var c2 = rawLine.IndexOf(',', c1 + 1);
        if (c2 < 0) continue;
        var line = rawLine[(c2 + 1)..];
        if (line.StartsWith('"') && line.EndsWith('"'))
            line = line[1..^1].Replace("\"\"", "\"");

        if (line.Contains("[EXP]")) { events.Add($"{rawLine[..c1]}  {line.Trim()}"); continue; }
        if (!line.Contains("pitch:") || !line.Contains("roll:")) continue;

        var d = new Dictionary<string, double>();
        foreach (var kv in line.Split(','))
        {
            var i = kv.IndexOf(':');
            if (i <= 0) continue;
            if (double.TryParse(kv[(i + 1)..], System.Globalization.CultureInfo.InvariantCulture, out var v))
                d[kv[..i].Trim()] = v;
        }
        if (d.ContainsKey("pitch") && d.ContainsKey("roll")) samples.Add(d);
    }

    if (samples.Count < 20) { Fail($"analyze: only {samples.Count} telemetry rows found — run 'telemetry on' (or 'telemetry fast') while logging."); return 1; }

    double durS;
    if (samples[0].ContainsKey("t") && samples[^1].ContainsKey("t"))
        durS = (samples[^1]["t"] - samples[0]["t"]) / 1000.0;
    else durS = samples.Count / 20.0;
    double rate = samples.Count / Math.Max(0.001, durS);

    Console.WriteLine($"[36m=== bb8 analyze — {Path.GetFileName(file)} ===[0m");
    Console.WriteLine($"samples: {samples.Count}   duration: {durS:F1}s   effective rate: {rate:F1} Hz");

    var report = new List<string>();

    void Axis(string name, string pwmKey)
    {
        var x = samples.Where(s => s.ContainsKey(name)).Select(s => s[name]).ToArray();
        if (x.Length < 20) return;
        double mean = x.Average();
        double sd = Math.Sqrt(x.Select(v => (v - mean) * (v - mean)).Average());
        double min = x.Min(), max = x.Max();

        // dominant oscillation via hysteresis zero-crossings of (x - mean)
        double hyst = Math.Max(0.15, 0.5 * sd);
        int crossings = 0; int sign = 0;
        foreach (var v in x)
        {
            var e = v - mean;
            if (sign >= 0 && e < -hyst) { if (sign > 0) crossings++; sign = -1; }
            else if (sign <= 0 && e > hyst) { if (sign < 0) crossings++; sign = 1; }
            else if (sign == 0 && Math.Abs(e) > hyst) sign = Math.Sign(e);
        }
        double freq = crossings / 2.0 / Math.Max(0.001, durS);

        double satPct = 0, meanAbsPwm = 0;
        var pwm = samples.Where(s => s.ContainsKey(pwmKey)).Select(s => s[pwmKey]).ToArray();
        if (pwm.Length > 0)
        {
            satPct = 100.0 * pwm.Count(v => Math.Abs(v) >= 250) / pwm.Length;
            meanAbsPwm = pwm.Select(Math.Abs).Average();
        }

        Console.WriteLine($"\n[33m{name}[0m  mean {mean,7:F2}   sigma {sd,6:F2}   range [{min:F2} .. {max:F2}]");
        Console.WriteLine($"       oscillation ~{freq:F2} Hz ({crossings} crossings)   {pwmKey}: mean|PWM| {meanAbsPwm:F0}, saturated {satPct:F0}%");

        bool activelyDriven = meanAbsPwm > 15;
        if (!activelyDriven && sd < 0.3)
            report.Add($"{name}: static capture — bias {mean:F2} deg, noise sigma {sd:F2} deg. Good baseline; if |bias| > 1 deg re-run level calibration.");
        if (activelyDriven && sd > 1.5 && freq is > 0.3 and < 6)
            report.Add($"{name}: sustained ~{freq:F1} Hz oscillation (amplitude ~{sd * 1.41:F1} deg) while driven -> lower that loop's Kp ~30% or raise Kd ~50%.");
        if (satPct > 30)
            report.Add($"{pwmKey}: saturated {satPct:F0}% of the time -> gains (or experiment amplitude) too high; the loop can't act linearly.");
        if (activelyDriven && sd > 4)
            report.Add($"{name}: very large swings ({sd:F1} deg sigma) -> check the sign conventions first (S2S_BALANCE_INVERT / GYRO_*_SIGN) before touching gains.");
    }

    Axis("pitch", "drv");
    Axis("roll", "s2s");

    // pot tracking quality
    var track = samples.Where(s => s.ContainsKey("pot") && s.ContainsKey("tgt")).Select(s => Math.Abs(s["tgt"] - s["pot"])).ToArray();
    if (track.Length > 20)
    {
        double meanErr = track.Average(), p95 = track.OrderBy(v => v).ElementAt((int)(track.Length * 0.95));
        Console.WriteLine($"\n[33mS2S position[0m  mean |tgt-pot| {meanErr:F0} counts   p95 {p95:F0} counts");
        if (meanErr > 40) report.Add($"S2S inner loop: mean position error {meanErr:F0} counts -> raise 'pref innerkp' by 0.1-0.2, or the mechanism is binding.");
    }

    var hz = samples.Where(s => s.ContainsKey("hz")).Select(s => s["hz"]).ToArray();
    if (hz.Length > 0 && hz.Average() < 400)
        report.Add($"loop rate averaged {hz.Average():F0} Hz (healthy is 500+) -> something is stalling the ESP32 loop; turn off extra debug flags.");

    if (events.Count > 0)
    {
        Console.WriteLine($"\n[36mExperiments in this capture:[0m");
        foreach (var e in events.Take(20)) Console.WriteLine($"  {e}");
    }

    Console.WriteLine($"\n[36mAssessment:[0m");
    if (report.Count == 0) Console.WriteLine("  Nothing alarming — angles quiet, no sustained oscillation, no saturation.");
    foreach (var r in report) Console.WriteLine($"  [33m-[0m {r}");
    Console.WriteLine("\nDeeper dive: hand this CSV to Claude in the BB8 workspace — it reads the physics out of it.");
    return 0;
}

(int rc, string stdout, string stderr) Run(string file, string arguments, bool capture)
{
    var psi = new ProcessStartInfo(file, arguments)
    {
        RedirectStandardOutput = capture,
        RedirectStandardError = capture,
        UseShellExecute = false
    };
    using var p = Process.Start(psi)!;
    string so = "", se = "";
    if (capture)
    {
        so = p.StandardOutput.ReadToEnd();
        se = p.StandardError.ReadToEnd();
    }
    p.WaitForExit();
    return (p.ExitCode, so, se);
}

void Fail(string msg) => Console.WriteLine($"\u001b[31m[ERROR] {msg}\u001b[0m");

// ------------------------------------------------------------------
record PortInfo(string Port, string? Vid, string? Pid);

class Channel(string label, string portName, int baud, string color)
{
    public string Label { get; } = label;
    public string PortName { get; } = portName;
    public int Baud { get; } = baud;
    public string Color { get; } = color;
    public SerialPort? Port { get; private set; }
    public StringBuilder LineBuf { get; } = new();
    public DateTime LastRetry { get; set; } = DateTime.MinValue;
    public bool WasConnected { get; set; }
    public DateTime OpenedAt { get; set; }
    public bool VersionSent { get; set; }

    public bool TryOpen()
    {
        try
        {
            Port = new SerialPort(PortName, Baud)
            {
                ReadTimeout = 50,
                NewLine = "\n",
                DtrEnable = true,   // keep DTR asserted so USB-CDC boards (32u4/Trinket) talk
                RtsEnable = true
            };
            Port.Open();
            WasConnected = true;
            OpenedAt = DateTime.Now;
            VersionSent = false;
            return true;
        }
        catch (Exception)
        {
            Port?.Dispose();
            Port = null;
            return false;
        }
    }

    public void Close()
    {
        try { Port?.Close(); } catch { }
        Port?.Dispose();
        Port = null;
    }
}

class MonitorSession(List<Channel> channels, StreamWriter? log, bool raw, bool showTlm)
{
    const string RESET = "\u001b[0m", DIM = "\u001b[90m", RED = "\u001b[31m",
                 GREEN = "\u001b[32m", INPUT = "\u001b[97m", BOLD = "\u001b[1m";

    readonly List<string> _history = new();
    int _histIdx = -1;
    readonly StringBuilder _input = new();
    int _active;
    string _tlm = "";
    string _tlmBoard = "";
    DateTime _tlmAt = DateTime.MinValue;
    int _rows, _cols;
    bool _quit;

    public int Run()
    {
        EnableVt();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; _quit = true; };

        if (raw) return RunRaw();

        _rows = SafeRows(); _cols = SafeCols();
        SetupScreen();

        Emit($"{DIM}bb8 monitor — {string.Join(", ", channels.Select(c => $"{c.Color}{c.Label}{DIM}@{c.PortName}"))}{RESET}");
        Emit($"{DIM}type + Enter to send · Up/Down history · Tab switch board · Esc or Ctrl+C exit{RESET}");
        foreach (var ch in channels.Where(c => c.Port is null))
            Emit($"{RED}[{ch.Label}] could not open {ch.PortName} — retrying in background{RESET}");

        DrawStatus();
        DrawInput();

        while (!_quit)
        {
            bool activity = PumpSerial();
            activity |= PumpKeys();
            CheckResize();
            if ((DateTime.Now - _tlmAt).TotalMilliseconds < 2500) DrawStatus();
            if (!activity) Thread.Sleep(8);
        }

        TeardownScreen();
        Console.WriteLine("[MONITOR] Closed.");
        return 0;
    }

    int RunRaw()
    {
        while (!_quit)
        {
            bool activity = PumpSerial();
            if (!activity) Thread.Sleep(10);
        }
        return 0;
    }

    // ---------- serial ----------
    bool PumpSerial()
    {
        bool any = false;
        foreach (var ch in channels)
        {
            if (ch.Port is null)
            {
                if ((DateTime.Now - ch.LastRetry).TotalSeconds >= 2)
                {
                    ch.LastRetry = DateTime.Now;
                    if (ch.TryOpen())
                        Emit($"{GREEN}[{ch.Label}] reconnected on {ch.PortName}{RESET}");
                }
                continue;
            }
            // Ask the board to identify itself shortly after connecting —
            // UART-bridged ESP32s can't detect a monitor attaching on their own
            if (!ch.VersionSent && (DateTime.Now - ch.OpenedAt).TotalMilliseconds > 700)
            {
                ch.VersionSent = true;
                try { ch.Port.WriteLine("version"); } catch (IOException) { }
            }
            string chunk;
            try { chunk = ch.Port.ReadExisting(); }
            catch (Exception)
            {
                Emit($"{RED}[{ch.Label}] lost {ch.PortName} — will retry{RESET}");
                ch.Close();
                continue;
            }
            if (chunk.Length == 0) continue;
            any = true;
            foreach (var c in chunk)
            {
                if (c == '\n')
                {
                    HandleLine(ch, ch.LineBuf.ToString().TrimEnd('\r'));
                    ch.LineBuf.Clear();
                }
                else if (ch.LineBuf.Length < 512) ch.LineBuf.Append(c);
            }
        }
        return any;
    }

    void HandleLine(Channel ch, string line)
    {
        if (line.Length == 0) return;
        var ts = DateTime.Now.ToString("HH:mm:ss.fff");
        log?.WriteLine($"{ts},{ch.Label},{Csv(line)}");

        bool isTlm = line.Contains("pitch:") && line.Contains("roll:");
        if (isTlm)
        {
            _tlm = line;
            _tlmBoard = ch.Label;
            _tlmAt = DateTime.Now;
            if (!raw) DrawStatus();
            if (!showTlm) return;              // live bar instead of scroll
        }

        var prefix = channels.Count > 1 ? $"{ch.Color}[{ch.Label}]{RESET} " : "";
        Emit($"{DIM}{ts}{RESET} {prefix}{Colorize(line)}");
    }

    static string Csv(string s) => s.Contains(',') || s.Contains('"')
        ? "\"" + s.Replace("\"", "\"\"") + "\"" : s;

    static string Colorize(string line)
    {
        var color = line.StartsWith("[ERROR]") || line.Contains("FATAL") ? "\u001b[31m"
                  : line.StartsWith("[SAFETY]") ? "\u001b[91m"
                  : line.StartsWith("[BOOT") || line.Contains("===") ? "\u001b[36m"
                  : line.StartsWith("[PID") ? "\u001b[32m"
                  : line.StartsWith("[CAL") || line.StartsWith("[CFG]") || line.StartsWith("[NVS]") ? "\u001b[94m"
                  : line.StartsWith("[TOGGLE]") ? "\u001b[95m"
                  : "";
        return color.Length > 0 ? $"{color}{line}{RESET}" : line;
    }

    // ---------- keyboard ----------
    bool PumpKeys()
    {
        bool any = false;
        while (Console.KeyAvailable)
        {
            any = true;
            var key = Console.ReadKey(intercept: true);
            switch (key.Key)
            {
                case ConsoleKey.Enter:
                {
                    var cmd = _input.ToString();
                    _input.Clear();
                    _histIdx = -1;
                    if (cmd.Length > 0)
                    {
                        if (_history.Count == 0 || _history[^1] != cmd) _history.Add(cmd);
                        var ch = channels[_active];
                        if (ch.Port is not null)
                        {
                            try
                            {
                                ch.Port.WriteLine(cmd);
                                Emit($"{DIM}{DateTime.Now:HH:mm:ss.fff}{RESET} {ch.Color}[{ch.Label}]{RESET} \u001b[97m>> {cmd}{RESET}");
                                log?.WriteLine($"{DateTime.Now:HH:mm:ss.fff},{ch.Label},>> {Csv(cmd)}");
                            }
                            catch (Exception ex) { Emit($"{RED}[{ch.Label}] write failed: {ex.Message}{RESET}"); }
                        }
                        else Emit($"{RED}[{ch.Label}] not connected{RESET}");
                    }
                    DrawInput();
                    break;
                }
                case ConsoleKey.Backspace:
                    if (_input.Length > 0) { _input.Length--; DrawInput(); }
                    break;
                case ConsoleKey.UpArrow:
                    if (_history.Count > 0)
                    {
                        _histIdx = _histIdx < 0 ? _history.Count - 1 : Math.Max(0, _histIdx - 1);
                        _input.Clear(); _input.Append(_history[_histIdx]);
                        DrawInput();
                    }
                    break;
                case ConsoleKey.DownArrow:
                    if (_histIdx >= 0)
                    {
                        _histIdx++;
                        _input.Clear();
                        if (_histIdx < _history.Count) _input.Append(_history[_histIdx]);
                        else _histIdx = -1;
                        DrawInput();
                    }
                    break;
                case ConsoleKey.Tab:
                    _active = (_active + 1) % channels.Count;
                    DrawInput();
                    DrawStatus();
                    break;
                case ConsoleKey.Escape:
                    _quit = true;
                    break;
                default:
                    if (!char.IsControl(key.KeyChar) && _input.Length < 200)
                    {
                        _input.Append(key.KeyChar);
                        DrawInput();
                    }
                    break;
            }
        }
        return any;
    }

    // ---------- screen ----------
    void SetupScreen()
    {
        // Scroll region = rows 1..H-2; status bar on H-1, input on H.
        Console.Write($"\u001b[1;{_rows - 2}r");
        Console.Write($"\u001b[{_rows - 2};1H");
    }

    void TeardownScreen()
    {
        Console.Write("\u001b[r");                    // reset scroll region
        Console.Write($"\u001b[{_rows};1H\u001b[2K"); // clear input row
        Console.Write($"\u001b[{_rows - 1};1H\u001b[2K\n");
    }

    void CheckResize()
    {
        var r = SafeRows(); var c = SafeCols();
        if (r != _rows || c != _cols)
        {
            _rows = r; _cols = c;
            SetupScreen();
            DrawStatus();
            DrawInput();
        }
    }

    void Emit(string line)
    {
        if (raw) { Console.WriteLine(StripIfRedirected(line)); return; }
        // Print inside the scroll region at the current cursor position
        Console.Write(line + "\n");
        DrawInput();  // keep the input line intact after scrolling output
    }

    static string StripIfRedirected(string s) => Console.IsOutputRedirected
        ? System.Text.RegularExpressions.Regex.Replace(s, "\u001b\\[[0-9;]*m", "") : s;

    void DrawStatus()
    {
        if (raw) return;
        string bar;
        if (_tlm.Length > 0 && (DateTime.Now - _tlmAt).TotalSeconds < 3)
        {
            bar = $"\u001b[30;43m TLM {RESET} {BOLD}{FormatTlm(_tlm)}{RESET}";
        }
        else
        {
            var states = string.Join("  ", channels.Select((c, i) =>
                $"{(i == _active ? BOLD : "")}{c.Color}{c.Label}{RESET}{DIM}:{(c.Port is not null ? "up" : "down")}{RESET}"));
            bar = $"{DIM}— no telemetry (send 'telemetry on') —{RESET}  {states}";
        }
        Console.Write($"\u001b7\u001b[{_rows - 1};1H\u001b[2K{Truncate(bar)}\u001b8");
    }

    string FormatTlm(string line)
    {
        // pitch:-1.23,roll:0.45,pot:1502,tgt:1500,drv:0,s2s:35,... -> compact readout
        var parts = line.Split(',');
        var sb = new StringBuilder();
        foreach (var p in parts)
        {
            var kv = p.Split(':', 2);
            if (kv.Length != 2) continue;
            sb.Append($"{DIM}{kv[0]}{RESET} {kv[1]}  ");
        }
        return sb.ToString().TrimEnd();
    }

    void DrawInput()
    {
        if (raw) return;
        var ch = channels[_active];
        var prompt = $"{ch.Color}[{ch.Label}]{RESET}{INPUT}> {_input}{RESET}";
        Console.Write($"\u001b7\u001b[{_rows};1H\u001b[2K{Truncate(prompt)}\u001b8");
    }

    string Truncate(string s)
    {
        // crude visible-length cap: strip ANSI to measure
        var visible = System.Text.RegularExpressions.Regex.Replace(s, "\u001b\\[[0-9;]*[a-zA-Z]|\u001b[78]", "");
        if (visible.Length <= _cols - 1) return s;
        return s[..Math.Min(s.Length, _cols + s.Length - visible.Length - 1)];
    }

    static int SafeRows() { try { return Math.Max(8, Console.WindowHeight); } catch { return 30; } }
    static int SafeCols() { try { return Math.Max(40, Console.WindowWidth); } catch { return 120; } }

    // ---------- VT enable (classic conhost needs this; Windows Terminal has it on) ----------
    [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int nStdHandle);
    [DllImport("kernel32.dll")] static extern bool GetConsoleMode(IntPtr h, out uint mode);
    [DllImport("kernel32.dll")] static extern bool SetConsoleMode(IntPtr h, uint mode);

    static void EnableVt()
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return;
        try
        {
            var h = GetStdHandle(-11); // STD_OUTPUT_HANDLE
            if (GetConsoleMode(h, out var m)) SetConsoleMode(h, m | 0x0004); // ENABLE_VIRTUAL_TERMINAL_PROCESSING
        }
        catch { }
    }
}

public class Bb8Config
{
    [JsonPropertyName("arduinoCli")] public string ArduinoCli { get; set; } = "arduino-cli";
    [JsonPropertyName("sketchRoot")] public string SketchRoot { get; set; } = "";
    [JsonPropertyName("buildRoot")] public string BuildRoot { get; set; } = "";
    [JsonPropertyName("targets")] public List<Bb8Target> Targets { get; set; } = new();
}

public class Bb8Target
{
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("description")] public string Description { get; set; } = "";
    [JsonPropertyName("sketch")] public string Sketch { get; set; } = "";
    [JsonPropertyName("fqbn")] public string Fqbn { get; set; } = "";
    [JsonPropertyName("baud")] public int Baud { get; set; } = 115200;
    [JsonPropertyName("usbVid")] public string? UsbVid { get; set; }
    [JsonPropertyName("usbPid")] public string? UsbPid { get; set; }
    [JsonPropertyName("bannerMatch")] public string? BannerMatch { get; set; }
}

[JsonSerializable(typeof(Bb8Config))]
public partial class JsonCtx : JsonSerializerContext { }
