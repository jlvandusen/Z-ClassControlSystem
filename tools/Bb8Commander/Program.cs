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
//  bb8 update [--flash]             pull new firmware/tooling from GitHub
//  (every build/upload/deploy also checks GitHub first; --no-update skips)
// ============================================================

using System.Diagnostics;
using System.IO.Ports;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

VtConsole.Enable();   // ANSI colors on classic conhost too

var configPath = FindConfig();
if (configPath is null)
{
    Fail("targets.json not found (looked in current dir and C:\\Users\\james\\BB8).");
    return 1;
}
var config = JsonSerializer.Deserialize<Bb8Config>(File.ReadAllText(configPath), JsonCtx.Default.Bb8Config)!;

if (args.Length == 0) { PrintHelp(); return 0; }

const int REBUILD_EXIT = 75;   // tells bb8.cmd: bb8's own source changed - rebuild bin\ and re-run this command
var boolFlags = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    { "--raw", "--show-tlm", "--list", "--auto", "--install-driver", "--no-update", "--flash" };

try
{
    var cmd = args[0].ToLowerInvariant();
    if (cmd is not ("help" or "-h" or "--help" or "update") && !Flag("--no-update")
        && Environment.GetEnvironmentVariable("BB8_NO_UPDATE") is not ("1" or "true"))
    {
        var urc = await AutoUpdateCheck(cmd);
        if (urc != 0) return urc;
    }
    switch (cmd)
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
        case "tune":     return await CmdTune(Arg(1), Opt("--port"));
        case "pair":     return Flag("--install-driver") ? await CmdInstallPadDriver() : await CmdPair(Opt("--mac"), Opt("--port"), Flag("--list"));
        case "identify": return await CmdIdentify();
        case "update":   return await CmdUpdate(Flag("--flash"));
        case "sounds":   return await CmdSounds(Arg(1), Flag("--flash"));
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
        if (args[i].StartsWith("--")) { if (!boolFlags.Contains(args[i])) i++; continue; }  // skip option (+ its value)
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
    // Search order: BB8_HOME, the current directory (and its parents), the folder
    // bb8.exe lives in (and its parents — a self-contained install keeps
    // targets.json one level above bb8\), then the developer checkout.
    var starts = new List<string>();
    var home = Environment.GetEnvironmentVariable("BB8_HOME");
    if (!string.IsNullOrWhiteSpace(home)) starts.Add(home);
    starts.Add(Directory.GetCurrentDirectory());
    starts.Add(AppContext.BaseDirectory);
    starts.Add(@"C:\Users\james\BB8");
    foreach (var dir in starts)
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
          bb8 tune <s2s|drive|dome> [--nudges N]   LIVE closed-loop tuner (balance PID / dome tilt)
          bb8 pair [--list] [--auto] [--mac XX:..] guided PS3/Nav pairing + primary/secondary assignment
          bb8 pair --install-driver               install the libusb driver for PS3/Nav pads (UAC)
          bb8 identify                            probe ports, read boot banners
          bb8 update [--flash]                    pull new firmware/tooling from GitHub; --flash also reflashes
                                                  every plugged-in board whose firmware is older than its sketch
          bb8 sounds [E:] [--flash]               scan the DFPlayer SD, report bank coverage, regenerate the
                                                  PSI beep-envelopes (needs ffmpeg); --flash reflashes the dome

        Monitor (full-screen):
          - type + Enter          send a serial command to the ACTIVE board
                                  (help / telemetry on / pid show / debug s2s ...)
          - Up / Down             command history
          - Tab                   switch active board (when monitoring several)
          - q + Enter, Esc, or Ctrl+C   exit
          - telemetry lines render in the live status bar instead of scrolling
            (--show-tlm scrolls them too; --log always captures everything)
          - --raw = plain line streaming, no UI (for piping/CI)

        Updates:
          build / upload / deploy always check GitHub first (other commands: at most every 4 h),
          fast-forward this checkout when that is safe, and rebuild bb8 if its own source changed.
          Skip with --no-update or BB8_NO_UPDATE=1.

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
    // Stamp FIRST so the compile bakes this build number in (it used to stamp
    // after compiling, so every binary carried the previous build's label).
    var build = BumpBuild(t.Name);
    WriteBuildStamp(t, build);
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


    // Flash, then let the USB re-enumerate, then read the BANNER back. The
    // banner is the only judge of success: avrdude's exit code lies on the
    // 32u4/Trinket (it often reports a failure for a flash that landed, and
    // vice-versa). If the running build number != the stamp, flash again —
    // up to 3 attempts in total.
    bool nativeUsbBoot = IsNativeUsb(t);
    const int MAX_ATTEMPTS = 3;
    for (int a = 1; a <= MAX_ATTEMPTS; a++)
    {
        Console.WriteLine($"\u001b[36m[UPLOAD] {t.Name} -> {port}{(a > 1 ? $"  (attempt {a}/{MAX_ATTEMPTS})" : "")}\u001b[0m");
        var (urc, _, _) = Run(config.ArduinoCli,
            $"upload -p {port} --fqbn {t.Fqbn} --input-dir \"{buildPath}\" \"{sketch}\"", capture: false);
        if (urc != 0) Console.WriteLine("\u001b[33m[UPLOAD] avrdude/esptool reported an error — ignoring, the banner decides.\u001b[0m");

        int running = await ReadRunningBuild(t, port, nativeUsbBoot);
        if (running == build)
        {
            Console.WriteLine($"\u001b[32m[VERIFY] OK — {t.Name} is running build {build}.\u001b[0m");
            return 0;
        }
        Console.WriteLine(running < 0
            ? $"\u001b[33m[VERIFY] no banner from {t.Name} after attempt {a}.\u001b[0m"
            : $"\u001b[33m[VERIFY] {t.Name} still reports build {running} (wanted {build}) after attempt {a}.\u001b[0m");
        if (a < MAX_ATTEMPTS)
        {
            await Task.Delay(2500);
            // the board may have come back on a different COM number
            if (nativeUsbBoot)
                port = DetectPorts().Where(x => GuessTargets(x).Contains(t.Name)).Select(x => x.Port).FirstOrDefault() ?? port;
        }
    }
    Fail($"{t.Name} never confirmed build {build} after {MAX_ATTEMPTS} attempts.");
    if (nativeUsbBoot)
        Console.WriteLine("""
            [HINT] 32u4/Trinket: double-tap the reset button (LED pulses = bootloader, ~8 s),
                   'bb8 list' for the bootloader's COM, then 'bb8 upload <target> --port COMx'.
            """);
    return 1;
}

// Reads "build N" from the board's banner after a flash. Waits for the USB
// to drop and come back (native-USB boards), opens the port (the ESP32
// auto-resets on open and prints its BOOT banner), and also asks 'version'
// in case the boot banner was missed. Returns -1 if nothing answered.
async Task<int> ReadRunningBuild(Bb8Target t, string port, bool nativeUsb)
    => (await ReadBannerStamp(t, port, nativeUsb, quiet: false)).Build;

// Same probe, but keeps the git stamp and the raw banner text too (bb8 update --flash
// judges staleness from them). Raw is "" when nothing answered at all.
async Task<BannerStamp> ReadBannerStamp(Bb8Target t, string port, bool nativeUsb, bool quiet)
{
    if (!quiet) Console.WriteLine($"\u001b[36m[VERIFY] waiting for {t.Name} to re-enumerate and report its build...\u001b[0m");
    var raw = new StringBuilder();
    var deadline = DateTime.Now.AddSeconds(25);
    await Task.Delay(nativeUsb ? 3000 : 1500);
    while (DateTime.Now < deadline)
    {
        string? p = port;
        if (nativeUsb)
        {
            var cand = DetectPorts().Where(x => GuessTargets(x).Contains(t.Name)).Select(x => x.Port).ToList();
            p = cand.Contains(port) ? port : cand.FirstOrDefault();
            if (p is null) { await Task.Delay(600); continue; }
        }
        try
        {
            using var sp = new SerialPort(p, t.Baud) { ReadTimeout = 100, NewLine = "\n", DtrEnable = true, RtsEnable = true };
            sp.Open();
            var sb = new StringBuilder();
            var sw = Stopwatch.StartNew();
            long lastAsk = -2000;
            while (sw.ElapsedMilliseconds < 8000)
            {
                if (sw.ElapsedMilliseconds - lastAsk > 1500) { try { sp.WriteLine("version"); } catch { } lastAsk = sw.ElapsedMilliseconds; }
                try { sb.Append(sp.ReadExisting()); } catch (TimeoutException) { }
                var m = System.Text.RegularExpressions.Regex.Match(sb.ToString(),
                    @"(BOOT|VERSION|CONNECT|AFTER WAIT) \| ([^\r\n|]*?) \| build (\d+)(?: \| [^|\r\n]* \| git ([^\s|]+))?");
                if (m.Success)
                    return new BannerStamp(int.Parse(m.Groups[3].Value), m.Groups[4].Success ? m.Groups[4].Value : null,
                                           m.Groups[2].Value.Trim(), sb.ToString());
                await Task.Delay(100);
            }
            raw.Append(sb);
            return new BannerStamp(-1, null, null, raw.ToString());   // port opened, no build stamp in 8 s
        }
        catch (Exception ex)   // port busy / re-enumerating — retry
        {
            if (Environment.GetEnvironmentVariable("BB8_DEBUG") == "1")
                Console.WriteLine($"[90m[VERIFY] {p}: {ex.GetType().Name}: {ex.Message}[0m");
            await Task.Delay(700);
        }
    }
    return new BannerStamp(-1, null, null, raw.ToString());
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
        var connectCmds = new List<string> { "version" };
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
            if (t.ConnectCommands is { Count: > 0 }) connectCmds = t.ConnectCommands;
        }
        channels.Add(new Channel(label, port, chBaud, colors[i % colors.Length], connectCmds));
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

    Console.WriteLine($"\u001b[36m=== bb8 analyze — {Path.GetFileName(file)} ===\u001b[0m");
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

        Console.WriteLine($"\n\u001b[33m{name}\u001b[0m  mean {mean,7:F2}   sigma {sd,6:F2}   range [{min:F2} .. {max:F2}]");
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
        Console.WriteLine($"\n\u001b[33mS2S position\u001b[0m  mean |tgt-pot| {meanErr:F0} counts   p95 {p95:F0} counts");
        if (meanErr > 40) report.Add($"S2S inner loop: mean position error {meanErr:F0} counts -> raise 'pref innerkp' by 0.1-0.2, or the mechanism is binding.");
    }

    var hz = samples.Where(s => s.ContainsKey("hz")).Select(s => s["hz"]).ToArray();
    if (hz.Length > 0 && hz.Average() < 400)
        report.Add($"loop rate averaged {hz.Average():F0} Hz (healthy is 500+) -> something is stalling the ESP32 loop; turn off extra debug flags.");

    if (events.Count > 0)
    {
        Console.WriteLine($"\n\u001b[36mExperiments in this capture:\u001b[0m");
        foreach (var e in events.Take(20)) Console.WriteLine($"  {e}");
    }

    Console.WriteLine($"\n\u001b[36mAssessment:\u001b[0m");
    if (report.Count == 0) Console.WriteLine("  Nothing alarming — angles quiet, no sustained oscillation, no saturation.");
    foreach (var r in report) Console.WriteLine($"  \u001b[33m-\u001b[0m {r}");
    Console.WriteLine("\nDeeper dive: hand this CSV to Claude in the BB8 workspace — it reads the physics out of it.");
    return 0;
}

// ------------------------------------------------------------------
//  TUNE — live closed-loop tuners
//    bb8 tune s2s|drive [--nudges N] [--cycles N]   balance PID (drive board)
//    bb8 tune dome      [--rocks N]  [--cycles N]   dome tilt compensation (body board)
//  Every transient is logged to tune-<axis>-<HHmm>.csv for bb8 analyze / Claude.
// ------------------------------------------------------------------
async Task<int> CmdTune(string? axis, string? portOpt)
{
    axis = axis?.ToLowerInvariant();
    int samplesPer = int.TryParse(Opt("--nudges") ?? Opt("--rocks"), out var sp0) ? Math.Clamp(sp0, 1, 6) : 2;
    int cycles = int.TryParse(Opt("--cycles"), out var cy) ? Math.Clamp(cy, 1, 30) : 10;
    switch (axis)
    {
        case "s2s":
        case "drive": return await TuneBalance(axis, portOpt, samplesPer, cycles);
        case "dome":  return await TuneDome(portOpt, samplesPer, cycles);
        default:
            Fail("tune: axis must be 's2s' or 'drive' (balance PID) or 'dome' (tilt compensation).");
            return 1;
    }
}


async Task<int> TuneBalance(string axis, string? portOpt, int nudgesPer, int maxCycles)
{
    bool isS2s = axis == "s2s";
    string chan = isS2s ? "roll" : "pitch";
    string pwmKey = isS2s ? "s2s" : "drv";

    var t = ResolveTarget("drive");
    var port = portOpt ?? await AutoPort(t);
    if (port is null) { Fail("No drive port found. Close any monitor and plug the drive in."); return 1; }

    Console.WriteLine($"""
        [36m=== bb8 tune {axis} — live closed-loop tuner ({nudgesPer} nudge(s) per decision, max {maxCycles} cycles) ===[0m
        Droid on the ROLLERS, drive enabled (CIRCLE), autoBalance ON (CROSS).
        When prompted: nudge the top ~5 deg {(isS2s ? "SIDEWAYS" : "FORWARD")} and LET GO.
        Ctrl+C aborts (gains stay whatever was last set, not saved).
        """);

    TuneLink link;
    try { link = new TuneLink(port, t.Baud); }
    catch (Exception ex) { Fail($"Cannot open {port}: {ex.Message} (close the monitor?)"); return 1; }
    using var _ = link;
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; link.Quit = true; };
    var logPath = $"tune-{axis}-{DateTime.Now:HHmm}.csv";
    link.OpenLog(logPath);
    Console.WriteLine($"[LOG] {logPath}");

    // Opening the port resets the ESP32 — wait out the boot, then read gains
    Console.WriteLine("[TUNE] port open — board is rebooting, waiting for it to settle...");
    link.Pump(3500, null);
    double kp = 0, ki = 0, kd = 0;
    for (int attempt = 1; attempt <= 6 && kp == 0 && !link.Quit; attempt++)
    {
        link.Send("pid show");
        foreach (var l in link.Pump(1200, null))
        {
            var m = System.Text.RegularExpressions.Regex.Match(l,
                isS2s ? @"S2S: Kp=([\d.]+) Ki=([\d.]+) Kd=([\d.]+)" : @"Drive: Kp=([\d.]+) Ki=([\d.]+) Kd=([\d.]+)");
            if (m.Success)
            {
                kp = ParseD(m.Groups[1].Value); ki = ParseD(m.Groups[2].Value); kd = ParseD(m.Groups[3].Value);
            }
        }
        if (kp == 0 && attempt < 6) Console.WriteLine($"[TUNE] no answer yet (boot in progress?) — retry {attempt}/5");
    }
    if (kp == 0) { Fail("Could not read current PID gains (is this the drive board? monitor closed?)"); return 1; }
    Console.WriteLine($"\u001b[36m[TUNE] starting gains: Kp={kp:F1} Ki={ki:F1} Kd={kd:F2}\u001b[0m");
    link.Send("telemetry fast"); link.Pump(400, null);

    void SetGains()
    {
        link.Send($"pid set {axis} kp {kp:F1}"); link.Pump(250, null);
        link.Send($"pid set {axis} ki {ki:F1}"); link.Pump(250, null);
        link.Send($"pid set {axis} kd {kd:F2}"); link.Pump(250, null);
    }

    // PD-only while tuning on the rig (integral winds against zero-offset,
    // catastrophically so on the drive axis where the shell spins freely)
    if (ki > 0)
    {
        Console.WriteLine("\u001b[36m[TUNE] disabling integral during tuning (prevents roll-away on the rig)\u001b[0m");
        ki = 0; link.Send($"pid set {axis} ki 0"); link.Pump(250, null);
    }

    bool ready = false; var readyDeadline = DateTime.Now.AddSeconds(90);
    Console.WriteLine("[TUNE] waiting for drive ENABLED + autoBalance ON (CIRCLE then CROSS)...");
    while (!ready && DateTime.Now < readyDeadline && !link.Quit)
        link.Pump(400, d => { if (d.GetValueOrDefault("en") == 1 && d.GetValueOrDefault("bal") == 1) ready = true; });
    if (!ready) { Fail("Timed out waiting for drive+balance to be enabled."); return 1; }

    // zero-offset sentinel
    {
        double sumPwm = 0, sumAng = 0; int n = 0;
        link.Pump(2000, d => { sumPwm += Math.Abs(d.GetValueOrDefault(pwmKey)); sumAng += d[chan]; n++; });
        if (n > 20)
        {
            double meanPwm = sumPwm / n, meanAng = sumAng / n;
            if (Math.Abs(meanAng) > 2.5)
                Console.WriteLine($"\u001b[33m[TUNE] WARNING: {chan} reads {meanAng:F1} deg at rest — level zero is off. Recommend: disable, sit level, 'cfg calibrate {axis}', rerun.\u001b[0m");
            else if (meanPwm > 40)
                Console.WriteLine($"\u001b[33m[TUNE] WARNING: motor averaging {meanPwm:F0} PWM at rest — residual windup or zero drift.\u001b[0m");
        }
    }
    Console.WriteLine("\u001b[32m[TUNE] beginning cycles\u001b[0m");

    Transient? Measure(int cycle, int nudgeIx)
    {
        Console.WriteLine($"\u001b[33m[TUNE {cycle}/{maxCycles} · nudge {nudgeIx}/{nudgesPer}] >>> NUDGE ~5 deg and LET GO <<<\u001b[0m");
        double baseline = 0; int baseN = 0;
        link.Pump(800, d => { baseline += d[chan]; baseN++; });
        if (baseN > 0) baseline /= baseN;

        bool kicked = false; var kickDeadline = DateTime.Now.AddSeconds(25);
        while (!kicked && DateTime.Now < kickDeadline && !link.Quit)
            link.Pump(100, d => { if (Math.Abs(d[chan] - baseline) > 3.0) kicked = true; });
        if (!kicked) { Console.WriteLine("[TUNE] no nudge detected in 25 s"); return null; }

        var samp = new List<double>(900); int satC = 0, total = 0;
        link.Pump(8000, d => { samp.Add(d[chan] - baseline); total++; if (Math.Abs(d.GetValueOrDefault(pwmKey)) >= 250) satC++; });
        if (samp.Count < 100) return null;

        double peak = samp.Take(120).Select(Math.Abs).Max();
        int overshoots = 0, sign = 0; double th = Math.Max(0.8, peak * 0.15);
        foreach (var v in samp)
        {
            if (sign >= 0 && v < -th) { if (sign > 0) overshoots++; sign = -1; }
            else if (sign <= 0 && v > th) { if (sign < 0) overshoots++; sign = 1; }
        }
        double settleT = 8.0;
        for (int i = 0; i < samp.Count - 50; i++)
            if (samp.Skip(i).Take(50).All(v => Math.Abs(v) < 1.2)) { settleT = i / 100.0; break; }
        var tail = samp.Skip((int)(samp.Count * 0.7)).ToList();
        double tm = tail.Average();
        double tailSigma = Math.Sqrt(tail.Select(v => (v - tm) * (v - tm)).Average());
        var tr = new Transient(peak, overshoots, settleT, tailSigma, 100.0 * satC / Math.Max(1, total));
        Console.WriteLine($"[TUNE]   peak={tr.Peak:F1} overshoots={tr.Overshoots} settle={tr.SettleT:F1}s tailSigma={tr.TailSigma:F2} sat={tr.SatPct:F0}%");
        link.LogNote($"transient,cycle={cycle},nudge={nudgeIx},kp={kp:F2},ki={ki:F2},kd={kd:F3},peak={tr.Peak:F2},overshoots={tr.Overshoots},settle={tr.SettleT:F2},tailSigma={tr.TailSigma:F3},sat={tr.SatPct:F1}");
        return tr;
    }

    int goodStreak = 0;
    for (int cycle = 1; cycle <= maxCycles && !link.Quit; cycle++)
    {
        var set = new List<Transient>();
        for (int n = 1; n <= nudgesPer && !link.Quit; n++)
        {
            var tr = Measure(cycle, n);
            if (tr is not null) set.Add(tr);
        }
        if (set.Count == 0) { Console.WriteLine("[TUNE] no usable transients this cycle — retrying"); continue; }

        double peak = set.Average(x => x.Peak), over = set.Average(x => x.Overshoots),
               settle = set.Average(x => x.SettleT), tails = set.Average(x => x.TailSigma),
               sat = set.Average(x => x.SatPct);
        Console.WriteLine($"\u001b[36m[TUNE] cycle {cycle} avg of {set.Count}: overshoots={over:F1} settle={settle:F1}s tailSigma={tails:F2} sat={sat:F0}%\u001b[0m");

        string verdict;
        if (tails > 2.0 || settle >= 7.9)
        { verdict = "STILL OSCILLATING"; kp *= 0.65; kd *= 1.15; goodStreak = 0; }
        else if (over >= 2.5)
        { verdict = "RINGING"; kp *= 0.8; kd *= 1.2; goodStreak = 0; }
        else if (over <= 1.2 && settle < 2.0)
        {
            goodStreak++;
            if (goodStreak == 1 && isS2s && ki < 2) { verdict = "GOOD — adding centering Ki"; ki = 3; }
            else if (goodStreak >= 2 || (!isS2s && cycle > 1))
            { Console.WriteLine("\u001b[32m[TUNE] GOOD — confirmed\u001b[0m"); break; }
            else verdict = "GOOD — confirming";
        }
        else if (over < 0.5 && settle > 3.0)
        { verdict = "SLUGGISH"; kp *= 1.25; goodStreak = 0; }
        else
        { verdict = "ACCEPTABLE — small refine"; kd *= 1.1; goodStreak = 0; }

        kp = Math.Clamp(kp, 1, isS2s ? 200 : 100); kd = Math.Clamp(kd, 0, 20); ki = Math.Clamp(ki, 0, 100);
        Console.WriteLine($"\u001b[36m[TUNE] {verdict} -> Kp={kp:F1} Ki={ki:F1} Kd={kd:F2}\u001b[0m");
        link.LogNote($"decision,cycle={cycle},verdict={verdict},kp={kp:F2},ki={ki:F2},kd={kd:F3}");
        SetGains();
    }

    if (!link.Quit)
    {
        link.Send("pid save"); link.Pump(600, null);
        Console.WriteLine($"\u001b[32m[TUNE] DONE — saved {axis}: Kp={kp:F1} Ki={ki:F1} Kd={kd:F2}\u001b[0m");
        if (!isS2s) Console.WriteLine("\u001b[36m[TUNE] drive Ki left at 0 for the rig. On the floor, if it drifts or won't hold a slope: 'pid set drive ki 2' then 'pid save'.\u001b[0m");
    }
    else Console.WriteLine("[TUNE] aborted — gains left as last set, NOT saved.");
    link.Send("telemetry off"); link.Pump(300, null);
    return 0;
}

// Dome tilt compensation: the body's servos should counter the body's
// tilt so the dome stays level. We can't see the dome, but we CAN measure
// how faithfully the servo output follows the commanded tilt: lag,
// amplitude ratio, roughness. Tunes alpha (smoothing) and slew; gain and
// invert are judged by eye and set with 'tilt gain' / 'tilt invert'.
async Task<int> TuneDome(string? portOpt, int rocksPer, int maxCycles)
{
    var t = ResolveTarget("body");
    var port = portOpt ?? await AutoPort(t);
    if (port is null) { Fail("No body port found. Plug the 32u4 in (and close any monitor)."); return 1; }

    Console.WriteLine($"""
        [36m=== bb8 tune dome — tilt-compensation tuner ({rocksPer} rock(s) per decision, max {maxCycles} cycles) ===[0m
        Drive enabled (CIRCLE) + autoBalance ON (CROSS) so body tilt drives the dome servos.
        When prompted: ROCK the droid side-to-side steadily, ~1 Hz, for 6 seconds.
        The dome should lean OPPOSITE the body (stay level). If it leans WITH the body, abort and
        run 'tilt invert x' (or 'tilt invert y' for pitch) on the body console first.
        """);

    TuneLink link;
    try { link = new TuneLink(port, t.Baud); }
    catch (Exception ex) { Fail($"Cannot open {port}: {ex.Message} (close the monitor?)"); return 1; }
    using var _ = link;
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; link.Quit = true; };
    var logPath = $"tune-dome-{DateTime.Now:HHmm}.csv";
    link.OpenLog(logPath);
    Console.WriteLine($"[LOG] {logPath}");
    link.Pump(800, null);

    double gain = 0, alpha = 0, slew = 0;
    for (int attempt = 1; attempt <= 5 && alpha == 0 && !link.Quit; attempt++)
    {
        link.Send("tilt show");
        foreach (var l in link.Pump(900, null))
        {
            var m = System.Text.RegularExpressions.Regex.Match(l, @"gain=([\d.]+) alpha=([\d.]+) slew=([\d.]+)");
            if (m.Success) { gain = ParseD(m.Groups[1].Value); alpha = ParseD(m.Groups[2].Value); slew = ParseD(m.Groups[3].Value); }
        }
    }
    if (alpha == 0) { Fail("Body did not answer 'tilt show' — is it running RC4.2 firmware? (bb8 upload body)"); return 1; }
    Console.WriteLine($"\u001b[36m[TUNE] starting: gain={gain:F2} alpha={alpha:F2} slew={slew:F0}\u001b[0m");
    link.Send("telemetry on"); link.Pump(300, null);

    bool ready = false; var deadline = DateTime.Now.AddSeconds(90);
    Console.WriteLine("[TUNE] waiting for drive ENABLED + autoBalance ON (on the drive controller)...");
    while (!ready && DateTime.Now < deadline && !link.Quit)
        link.Pump(400, d => { if (d.GetValueOrDefault("en") == 1 && d.GetValueOrDefault("bal") == 1) ready = true; });
    if (!ready) { Fail("Timed out waiting for drive+balance."); return 1; }

    void Apply()
    {
        link.Send($"tilt alpha {alpha:F2}"); link.Pump(250, null);
        link.Send($"tilt slew {slew:F0}"); link.Pump(250, null);
    }

    (double lagMs, double ratio, double rough, double amp)? Measure(int cycle, int ix)
    {
        Console.WriteLine($"\u001b[33m[TUNE {cycle}/{maxCycles} · rock {ix}/{rocksPer}] >>> ROCK side-to-side ~1 Hz for 6 s <<<\u001b[0m");
        bool moving = false; var kd = DateTime.Now.AddSeconds(25); double b = 0; int bn = 0;
        link.Pump(600, d => { b += d["roll"]; bn++; }); if (bn > 0) b /= bn;
        while (!moving && DateTime.Now < kd && !link.Quit)
            link.Pump(100, d => { if (Math.Abs(d["roll"] - b) > 4) moving = true; });
        if (!moving) { Console.WriteLine("[TUNE] no motion detected"); return null; }

        var tx = new List<double>(); var outX = new List<double>(); var ts = new List<double>();
        link.Pump(6000, d =>
        {
            if (!d.ContainsKey("tx") || !d.ContainsKey("l") || !d.ContainsKey("r")) return;
            tx.Add(d["tx"]);
            outX.Add(((d["l"] - 70.0) + (d["r"] - 110.0)) / 2.0);   // X component of actual servo position
            ts.Add(d["t"]);
        });
        int N = tx.Count;
        if (N < 100) return null;
        double dtMs = (ts[^1] - ts[0]) / Math.Max(1, N - 1);
        double Mean(List<double> a) => a.Average();
        double Sd(List<double> a) { var m = Mean(a); return Math.Sqrt(a.Select(v => (v - m) * (v - m)).Average()); }
        double sTx = Sd(tx), sOut = Sd(outX);
        if (sTx < 2) { Console.WriteLine($"[TUNE] commanded tilt amplitude only {sTx:F1} deg — rock harder (or raise 'tilt gain')"); return null; }

        double Corr(List<double> a, List<double> c, int lag)
        {
            int n = a.Count - lag; double ma = 0, mc = 0;
            for (int i = 0; i < n; i++) { ma += a[i]; mc += c[i + lag]; }
            ma /= n; mc /= n; double num = 0, da = 0, dc = 0;
            for (int i = 0; i < n; i++) { num += (a[i] - ma) * (c[i + lag] - mc); da += (a[i] - ma) * (a[i] - ma); dc += (c[i + lag] - mc) * (c[i + lag] - mc); }
            return num / Math.Sqrt(da * dc + 1e-9);
        }
        int maxLag = (int)Math.Min(N / 3, 600 / Math.Max(1, dtMs)); int bestLag = 0; double bestC = -2;
        for (int L = 0; L <= maxLag; L++) { var c = Corr(tx, outX, L); if (c > bestC) { bestC = c; bestLag = L; } }
        double lagMs = bestLag * dtMs;
        double ratio = sOut / sTx;
        // roughness: second-difference energy of the output relative to its motion
        double acc = 0; for (int i = 2; i < N; i++) acc += Math.Abs(outX[i] - 2 * outX[i - 1] + outX[i - 2]);
        double rough = (acc / (N - 2)) / Math.Max(0.1, sOut);
        Console.WriteLine($"[TUNE]   lag={lagMs:F0}ms ratio={ratio:F2} rough={rough:F3} amp={sTx:F1}deg corr={bestC:F2}");
        link.LogNote($"rock,cycle={cycle},ix={ix},alpha={alpha:F2},slew={slew:F0},lag={lagMs:F0},ratio={ratio:F3},rough={rough:F4},amp={sTx:F2}");
        return (lagMs, ratio, rough, sTx);
    }

    int good = 0;
    for (int cycle = 1; cycle <= maxCycles && !link.Quit; cycle++)
    {
        var set = new List<(double lagMs, double ratio, double rough, double amp)>();
        for (int i = 1; i <= rocksPer && !link.Quit; i++) { var r = Measure(cycle, i); if (r is not null) set.Add(r.Value); }
        if (set.Count == 0) continue;
        double lag = set.Average(x => x.lagMs), ratio = set.Average(x => x.ratio), rough = set.Average(x => x.rough);
        Console.WriteLine($"\u001b[36m[TUNE] cycle {cycle} avg: lag={lag:F0}ms ratio={ratio:F2} rough={rough:F3}\u001b[0m");

        string verdict;
        if (rough > 0.12)
        { verdict = "ROUGH/JITTERY — smoothing more"; alpha = Math.Max(0.08, alpha * 0.75); slew = Math.Max(80, slew * 0.85); good = 0; }
        else if (lag > 120 || ratio < 0.75)
        { verdict = "SLUGGISH — faster"; alpha = Math.Min(0.95, alpha * 1.3); slew = Math.Min(700, slew * 1.25); good = 0; }
        else if (lag <= 100 && ratio >= 0.8)
        {
            good++;
            if (good >= 2) { Console.WriteLine("\u001b[32m[TUNE] GOOD — confirmed\u001b[0m"); break; }
            verdict = "GOOD — confirming";
        }
        else { verdict = "ACCEPTABLE — small refine"; alpha = Math.Min(0.95, alpha * 1.1); good = 0; }

        Console.WriteLine($"\u001b[36m[TUNE] {verdict} -> alpha={alpha:F2} slew={slew:F0}\u001b[0m");
        link.LogNote($"decision,cycle={cycle},verdict={verdict},alpha={alpha:F2},slew={slew:F0}");
        Apply();
    }

    if (!link.Quit)
    {
        link.Send("tilt save"); link.Pump(500, null);
        Console.WriteLine($"\u001b[32m[TUNE] DONE — saved dome tilt: alpha={alpha:F2} slew={slew:F0} (gain={gain:F2} unchanged — set by eye with 'tilt gain')\u001b[0m");
    }
    else Console.WriteLine("[TUNE] aborted — params left as last set, NOT saved.");
    link.Send("telemetry off"); link.Pump(300, null);
    return 0;
}

static double ParseD(string s) => double.Parse(s, System.Globalization.CultureInfo.InvariantCulture);

// ------------------------------------------------------------------
//  PAIR — guided wizard: read the drive's Bluetooth MAC, then for every
//  PS3 / Nav pad plugged into USB: show its own MAC, write the drive as
//  its master, and store it in the drive as PRIMARY (drive) or
//  SECONDARY (dome) controller. Replaces SixaxisPairTool + hand-editing
//  DEFAULT_PREF_*_MAC in the sketch.
//
//  bb8 pair                 interactive wizard
//  bb8 pair --list          pads on USB: own MAC + current master
//  bb8 pair --mac XX:..     use this drive MAC (no serial needed)
//  bb8 pair --auto          no prompts: pair all pads now; 1st = drive, 2nd = dome
// ------------------------------------------------------------------
async Task<int> CmdPair(string? macOpt, string? portOpt, bool listOnly)
{
    bool auto = Flag("--auto") || Console.IsInputRedirected;

    if (listOnly)
    {
        var pads0 = Ps3Pair.FindPads();
        if (pads0.Count == 0) { Fail("No PS3 Sixaxis / DualShock 3 / Navigation controller on USB (use a DATA cable)."); return 1; }
        foreach (var p in pads0)
        {
            var own = Ps3Pair.ReadOwnMac(p); var cur = Ps3Pair.ReadMaster(p);
            Console.WriteLine($"  {p.Name,-22} pad MAC {(own is null ? "?" : Ps3Pair.Fmt(own)),-18} master {(cur is null ? "?" : Ps3Pair.Fmt(cur))}  via {p.Via}");
        }
        return 0;
    }

    Console.WriteLine("""
        [36m=== bb8 pair — controller pairing wizard ===[0m
        Step 1  read the drive's Bluetooth address
        Step 2  plug a pad in over USB -> I show its MAC and ask to pair it
        Step 3  choose PRIMARY (drive) or SECONDARY (dome); the choice is saved in the drive
        Repeat for each pad. q + Enter finishes.
        """);

    // ---- Step 1: drive MAC + a live serial session for 'bt prefer' ----
    byte[]? driveMac = macOpt is not null ? Ps3Pair.ParseMac(macOpt) : null;
    if (macOpt is not null && driveMac is null) { Fail($"--mac '{macOpt}' is not a MAC (XX:XX:XX:XX:XX:XX)."); return 1; }

    SerialPort? drive = null;
    var rxBuf = new StringBuilder();
    string DrainDrive(int ms)
    {
        if (drive is null) return "";
        var sw = Stopwatch.StartNew(); var got = new StringBuilder();
        while (sw.ElapsedMilliseconds < ms)
        {
            try { got.Append(drive.ReadExisting()); } catch (TimeoutException) { }
            Thread.Sleep(50);
        }
        return got.ToString();
    }

    var t = ResolveTarget("drive");
    var port = portOpt ?? await AutoPort(t);
    if (port is not null)
    {
        try
        {
            drive = new SerialPort(port, t.Baud) { ReadTimeout = 100, NewLine = "\n", DtrEnable = true, RtsEnable = true };
            drive.Open();
            Console.WriteLine($"\u001b[36m[PAIR] drive on {port} — it reboots on connect, reading its Bluetooth MAC...\u001b[0m");
            var sw = Stopwatch.StartNew(); long lastAsk = -9000;
            while (sw.ElapsedMilliseconds < 14000 && driveMac is null)
            {
                if (sw.ElapsedMilliseconds - lastAsk > 3000) { try { drive.WriteLine("bt mac"); } catch { } lastAsk = sw.ElapsedMilliseconds; }
                rxBuf.Append(DrainDrive(200));
                var m = System.Text.RegularExpressions.Regex.Match(rxBuf.ToString(), @"\[BT\] Host MAC: ([0-9A-Fa-f:]{17})");
                if (m.Success) driveMac = Ps3Pair.ParseMac(m.Groups[1].Value);
            }
        }
        catch (Exception ex) { Console.WriteLine($"\u001b[33m[PAIR] could not use {port}: {ex.Message}\u001b[0m"); drive = null; }
    }
    if (driveMac is null)
    {
        Fail(drive is null
            ? "Drive not on USB and no --mac given. Plug the drive in (close monitors), or pass --mac from its '[BT] Host MAC' banner."
            : "Drive never reported its MAC (pre-RC4.2 firmware?). Pass --mac from its boot banner, or reflash the drive.");
        drive?.Dispose();
        return 1;
    }
    Console.WriteLine($"\u001b[32m[PAIR] drive Bluetooth MAC: {Ps3Pair.Fmt(driveMac)}\u001b[0m");
    bool canStore = drive is not null;
    if (!canStore) Console.WriteLine("\u001b[33m[PAIR] (no serial link to the drive — pads will be paired, but primary/secondary can't be stored; do it later with 'bt prefer drive|dome <MAC>')\u001b[0m");

    string Ask(string prompt, string def)
    {
        if (auto) return def;
        Console.Write($"\u001b[33m{prompt}\u001b[0m");
        var s = Console.ReadLine();
        return string.IsNullOrWhiteSpace(s) ? def : s.Trim();
    }

    // ---- Step 2/3: watch for pads ----
    var seen = new HashSet<string>();
    int paired = 0, assigned = 0, autoSlot = 0;
    Console.WriteLine("\n\u001b[36m[PAIR] Plug a controller into USB now (DATA cable). q + Enter to finish.\u001b[0m");
    var idle = Stopwatch.StartNew();
    while (true)
    {
        if (!auto && Console.KeyAvailable)
        {
            var k = Console.ReadKey(intercept: true);
            if (k.KeyChar is 'q' or 'Q') break;
        }
        var pads = Ps3Pair.FindPads().Where(p => !seen.Contains(p.Path)).ToList();
        if (pads.Count == 0)
        {
            if (auto && (seen.Count > 0 || idle.ElapsedMilliseconds > 8000)) break;
            await Task.Delay(600);
            continue;
        }
        foreach (var p in pads)
        {
            seen.Add(p.Path);
            await Task.Delay(400);   // let the HID stack settle after enumeration
            var own = Ps3Pair.ReadOwnMac(p);
            var cur = Ps3Pair.ReadMaster(p);
            Console.WriteLine($"\n\u001b[36m[PAIR] detected: {p.Name}\u001b[0m");
            Console.WriteLine($"       pad MAC:        {(own is null ? "(unreadable)" : Ps3Pair.Fmt(own))}");
            Console.WriteLine($"       current master: {(cur is null ? "(unreadable)" : Ps3Pair.Fmt(cur))}{(cur is not null && cur.SequenceEqual(driveMac) ? "  (already this drive)" : "")}");

            var yn = Ask($"Pair this pad to the drive ({Ps3Pair.Fmt(driveMac)})? [Y/n] ", "y");
            if (yn.StartsWith("n", StringComparison.OrdinalIgnoreCase)) { Console.WriteLine("       skipped"); continue; }

            if (!Ps3Pair.WriteMaster(p, driveMac))
            {
                Console.WriteLine("\u001b[31m       write FAILED — try another USB port/cable, or run as Administrator. (Fallback: SixaxisPairTool with the MAC above)\u001b[0m");
                continue;
            }
            var back = Ps3Pair.ReadMaster(p);
            if (back is not null && back.SequenceEqual(driveMac)) { Console.WriteLine($"\u001b[32m       master written and verified: {Ps3Pair.Fmt(back)}\u001b[0m"); paired++; }
            else Console.WriteLine($"\u001b[33m       written, but read-back = {(back is null ? "unreadable" : Ps3Pair.Fmt(back))} — try once more\u001b[0m");

            if (own is null) { Console.WriteLine("\u001b[33m       pad MAC unreadable, so it can't be stored as primary/secondary (use 'bt list' on the drive after it connects, then 'bt prefer drive slot0').\u001b[0m"); continue; }
            if (!canStore) continue;

            string choice = auto ? (autoSlot++ == 0 ? "1" : "2")
                                 : Ask("Assign as [1] PRIMARY = drive pad   [2] SECONDARY = dome pad   [Enter] skip: ", "");
            if (choice is "1" or "2")
            {
                var which = choice == "1" ? "drive" : "dome";
                drive!.WriteLine($"bt prefer {which} {Ps3Pair.Fmt(own)}");
                var reply = DrainDrive(1200);
                if (reply.Contains("saved preferred"))
                {
                    Console.WriteLine($"\u001b[32m       stored in drive NVS as {(which == "drive" ? "PRIMARY (drive)" : "SECONDARY (dome)")}\u001b[0m");
                    assigned++;
                }
                else Console.WriteLine("\u001b[33m       drive did not acknowledge 'bt prefer' — it may run pre-RC4.2 firmware. Reflash, then: bt prefer " + which + " " + Ps3Pair.Fmt(own) + "\u001b[0m");
            }
            Console.WriteLine("\u001b[36m       Unplug this pad. Plug the next one, or q + Enter to finish.\u001b[0m");
        }
    }

    // ---- summary ----
    if (drive is not null)
    {
        drive.WriteLine("bt prefer show");
        var rep = DrainDrive(900);
        foreach (var l in rep.Split('\n').Select(x => x.Trim()).Where(x => x.StartsWith("[BT] preferred")))
            Console.WriteLine("  " + l);
        drive.Dispose();
    }
    Console.WriteLine($"""

        [32m[PAIR] done — {paired} pad(s) paired, {assigned} assignment(s) stored.[0m
        Next: unplug the pads, power the drive, press PS on each pad. On the drive console,
        'bt list' shows who landed in which slot; 'bt prefer drive slot0' can re-assign live.
        """);
    return 0;
}

// ------------------------------------------------------------------
//  PAIR --install-driver — bind PS3 / Nav pads to libusb-win32 from the
//  package in tools/drivers/ps3_controller (no SixaxisPairTool needed).
//  Runs pnputil elevated (one UAC prompt) and reports the binding.
// ------------------------------------------------------------------
async Task<int> CmdInstallPadDriver()
{
    var pkg = Path.Combine(RepoRootDir(), "tools", "drivers", "ps3_controller");
    var inf = Path.Combine(pkg, "ps3_controller.inf");
    if (!File.Exists(inf)) { Fail($"driver package not found: {inf}"); return 1; }

    Console.WriteLine("""
        [36m[DRIVER] Installing libusb-win32 for PS3 / Navigation controllers (VID 054C).[0m
        A UAC prompt will appear — accept it. Windows may also warn that it can't verify the
        publisher of the INF; choose "Install this driver software anyway".
        """);

    var log = Path.Combine(Path.GetTempPath(), "bb8_pad_driver.log");
    try { File.Delete(log); } catch { }
    var script = Path.Combine(Path.GetTempPath(), "bb8_pad_driver.ps1");
    File.WriteAllText(script, $$"""
        $log = '{{log}}'
        "=== $(Get-Date) ===" | Out-File $log
        pnputil /add-driver '{{inf}}' /install 2>&1 | Out-File $log -Append
        pnputil /scan-devices 2>&1 | Out-File $log -Append
        Start-Sleep -Seconds 3
        Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like 'USB\VID_054C&PID_042F*' -or $_.InstanceId -like 'USB\VID_054C&PID_0268*' } |
            Select-Object Status, Class, FriendlyName, InstanceId | Format-Table -AutoSize | Out-String | Out-File $log -Append
        """);

    var psi = new ProcessStartInfo("powershell.exe", $"-NoProfile -ExecutionPolicy Bypass -File \"{script}\"")
    {
        UseShellExecute = true,
        Verb = "runas"
    };
    try
    {
        using var p = Process.Start(psi)!;
        await p.WaitForExitAsync();
    }
    catch (Exception ex) { Fail($"elevation refused or failed: {ex.Message}"); return 1; }

    string output = File.Exists(log) ? File.ReadAllText(log) : "(no log written)";
    foreach (var line in output.Split('\n').Select(l => l.TrimEnd()).Where(l => l.Length > 0))
        Console.WriteLine("  " + line);

    bool bound = output.Contains("libusb-win32 devices");
    bool added = output.Contains("Driver package added successfully") || output.Contains("already") || output.Contains("Total driver packages:  1");
    if (bound)
        Console.WriteLine("\u001b[32m[DRIVER] pad is bound to libusb-win32 — 'bb8 pair --list' should now read it.\u001b[0m");
    else if (!output.Contains("VID_054C"))
        Console.WriteLine("\u001b[33m[DRIVER] package processed; no pad is plugged in right now. Plug one in — Windows binds it automatically.\u001b[0m");
    else
        Console.WriteLine("\u001b[33m[DRIVER] a pad is present but still not on libusb-win32. Unplug/replug it; if it stays on HIDClass, see the log above for pnputil's error.\u001b[0m");
    return bound || added ? 0 : 1;
}


// ------------------------------------------------------------------
//  bb8 sounds — SD card inventory + PSI envelope generation
//  Scans the DFPlayer SD (auto-detects a removable drive with \MP3, or takes
//  a path), regenerates firmware/ESP32_DOME_RC4/PsiEnvelopes.h (ffmpeg
//  decodes each track; 25 Hz RMS envelope, gated, peak-normalized, gamma),
//  prints the bank/trigger coverage report, and with --flash rebuilds and
//  reflashes the dome when the envelope set changed.
// ------------------------------------------------------------------
async Task<int> CmdSounds(string? source, bool flash)
{
    // 1. locate the MP3 folder
    string? mp3 = null;
    if (source is not null)
    {
        var p = source.TrimEnd('\\', '/');
        if (p.Length == 2 && p[1] == ':') p += "\\";
        if (Directory.Exists(Path.Combine(p, "MP3"))) mp3 = Path.Combine(p, "MP3");
        else if (Directory.Exists(p) && Path.GetFileName(p).Equals("MP3", StringComparison.OrdinalIgnoreCase)) mp3 = p;
        else { Fail($"No MP3 folder at '{source}'."); return 1; }
    }
    else
    {
        foreach (var d in DriveInfo.GetDrives())
        {
            try
            {
                if ((d.DriveType == DriveType.Removable || d.DriveType == DriveType.Fixed) &&
                    d.IsReady && d.Name != @"C:\" && Directory.Exists(Path.Combine(d.Name, "MP3")))
                { mp3 = Path.Combine(d.Name, "MP3"); break; }
            }
            catch { }
        }
        if (mp3 is null) { Fail("No SD card with an \\MP3 folder found. Plug the card in or pass the path: bb8 sounds E:"); return 1; }
    }
    Console.WriteLine($"\u001b[36m[SOUNDS] card: {mp3}\u001b[0m");

    // 2. inventory
    var tracks = new SortedDictionary<int, string>();
    var strays = new List<string>();
    foreach (var f in Directory.GetFiles(mp3))
    {
        var name = Path.GetFileName(f);
        var m = System.Text.RegularExpressions.Regex.Match(name, @"^(\d{4})\.[Mm][Pp]3$");
        if (m.Success) tracks[int.Parse(m.Groups[1].Value)] = f;
        else strays.Add(name);
    }
    var root = Path.GetDirectoryName(mp3)!;
    var rootMp3 = Directory.GetFiles(root, "*.mp3").Length;
    Console.WriteLine($"[SOUNDS] {tracks.Count} tracks (4-digit) on the card");
    if (strays.Count > 0) Console.WriteLine($"\u001b[33m[SOUNDS] ignored (bad names — DFPlayer needs NNNN.mp3): {string.Join(", ", strays)}\u001b[0m");
    if (rootMp3 > 0) Console.WriteLine($"\u001b[33m[SOUNDS] {rootMp3} mp3 file(s) in the card ROOT — never played, delete them\u001b[0m");

    // 3. bank / trigger coverage
    void Bank(string label, int lo, int hi, string trigger)
    {
        var have = tracks.Keys.Where(t => t >= lo && t <= hi).ToList();
        var missing = Enumerable.Range(lo, hi - lo + 1).Where(t => !tracks.ContainsKey(t)).ToList();
        var miss = missing.Count == 0 ? "complete" :
                   have.Count == 0 ? "\u001b[33mEMPTY\u001b[0m" :
                   $"missing {string.Join(",", missing.Select(t => t.ToString("0000")))}";
        Console.WriteLine($"  {label,-28} {have.Count,2}/{hi - lo + 1,-2}  {miss,-40}  {trigger}");
    }
    Console.WriteLine("[SOUNDS] bank coverage:");
    Bank("0001-0031 chatter", 1, 31, "D-pad UP roll; fixed 3/4/5, L1 10-13, dome-pad 16-19/21-23/28; 0001 = pad connect");
    Bank("0040-0049 excited", 40, 49, "L2 + D-pad RIGHT roll");
    Bank("0060-0063 state cues", 60, 63, "60 bootup · 61 shutdown/pad-drop/L3 · 62 dome-mode · 63 balance");
    Bank("0070-0074 PS blips", 70, 74, "PS enable/disable roll");
    Bank("0075-0079 extra blips", 75, 79, "L2 + D-pad LEFT rolls 70-79");
    Bank("0080-0089 alerts", 80, 89, "IMU-stale cutoff, experiment aborts");
    var other = tracks.Keys.Where(t => !(t is >= 1 and <= 31 or >= 40 and <= 49 or >= 60 and <= 63
                                       or >= 70 and <= 79 or >= 80 and <= 89)).ToList();
    if (other.Count > 0)
        Console.WriteLine($"  console-only (no trigger)       {string.Join(", ", other.Select(t => t.ToString("0000")))}");

    // 4. regenerate envelopes (ffmpeg)
    if (Run("ffmpeg", "-version", capture: true).rc != 0)
    { Fail("ffmpeg not found on PATH — needed to decode the tracks (https://ffmpeg.org)."); return 1; }
    const int STEP_MS = 40, RATE = 8000; const double GAMMA = 0.6, GATE = 0.06;
    int win = RATE * STEP_MS / 1000;
    var envs = new SortedDictionary<int, byte[]>();
    foreach (var (n, file) in tracks)
    {
        var psi = new ProcessStartInfo("ffmpeg", $"-v error -i \"{file}\" -ac 1 -ar {RATE} -f s16le -")
        { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
        using var pr = Process.Start(psi)!;
        using var ms = new MemoryStream();
        pr.StandardOutput.BaseStream.CopyTo(ms);
        pr.WaitForExit();
        var raw = ms.ToArray();
        int samples = raw.Length / 2;
        var rms = new List<double>();
        for (int i = 0; i + win <= samples; i += win)
        {
            double sum = 0;
            for (int j = 0; j < win; j++) { double v = BitConverter.ToInt16(raw, (i + j) * 2); sum += v * v; }
            rms.Add(Math.Sqrt(sum / win));
        }
        if (rms.Count == 0) { Console.WriteLine($"\u001b[33m  {n:0000}: decode produced no audio — skipped\u001b[0m"); continue; }
        double peak = Math.Max(rms.Max(), 1.0);
        var q = rms.Select(v => { var x = v / peak; if (x < GATE) x = 0; return (byte)Math.Round(255 * Math.Pow(x, GAMMA)); }).ToList();
        while (q.Count > 0 && q[^1] == 0) q.RemoveAt(q.Count - 1);
        envs[n] = q.ToArray();
    }

    var hdrPath = Path.Combine(RepoRootDir(), "firmware", "ESP32_DOME_RC4", "PsiEnvelopes.h");
    var oldTracks = File.Exists(hdrPath)
        ? System.Text.RegularExpressions.Regex.Matches(File.ReadAllText(hdrPath), @"\{ (\d+),").Select(m => int.Parse(m.Groups[1].Value)).ToHashSet()
        : new HashSet<int>();
    var sb = new StringBuilder();
    sb.Append("#pragma once\n");
    sb.Append($"// Auto-generated by 'bb8 sounds' on {DateTime.Now:yyyy-MM-dd} - DO NOT EDIT.\n");
    sb.Append("// 25 Hz amplitude envelopes (uint8) for every MP3/NNNN.mp3 on the SD card.\n");
    sb.Append("// The dome plays the envelope for the track number relayed in the psi field,\n");
    sb.Append($"// so the PSI flickers with the actual beeps. {envs.Count} tracks, {envs.Values.Sum(v => v.Length)} bytes.\n");
    sb.Append("#include <stdint.h>\n\n");
    sb.Append($"#define PSI_ENV_STEP_MS {STEP_MS}\n\n");
    sb.Append("struct PsiEnv { uint8_t track; uint16_t len; const uint8_t *data; };\n\n");
    foreach (var (n, q) in envs)
    {
        sb.Append($"static const uint8_t psiEnv_{n}[] = {{");
        for (int i = 0; i < q.Length; i++) { if (i % 24 == 0) sb.Append("\n  "); sb.Append(q[i]).Append(','); }
        sb.Append("\n};\n");
    }
    sb.Append("\nstatic const PsiEnv PSI_ENVS[] = {\n");
    foreach (var (n, q) in envs) sb.Append($"  {{ {n}, {q.Length}, psiEnv_{n} }},\n");
    sb.Append("};\n");
    sb.Append($"static const uint8_t PSI_ENV_COUNT = {envs.Count};\n\n");
    sb.Append("static inline const PsiEnv* psiEnvFor(uint8_t track) {\n");
    sb.Append("  for (uint8_t i = 0; i < PSI_ENV_COUNT; i++)\n");
    sb.Append("    if (PSI_ENVS[i].track == track) return &PSI_ENVS[i];\n");
    sb.Append("  return nullptr;\n}\n");

    var added = envs.Keys.Where(t => !oldTracks.Contains(t)).ToList();
    var removed = oldTracks.Where(t => !envs.ContainsKey(t)).OrderBy(t => t).ToList();
    bool changed = added.Count > 0 || removed.Count > 0 || !File.Exists(hdrPath) || File.ReadAllText(hdrPath) != sb.ToString();
    if (changed)
    {
        File.WriteAllText(hdrPath, sb.ToString());
        Console.WriteLine($"\u001b[32m[SOUNDS] PsiEnvelopes.h regenerated: {envs.Count} tracks" +
            (added.Count > 0 ? $", +{string.Join(",", added.Select(t => t.ToString("0000")))}" : "") +
            (removed.Count > 0 ? $", -{string.Join(",", removed.Select(t => t.ToString("0000")))}" : "") + "\u001b[0m");
    }
    else Console.WriteLine("[SOUNDS] envelopes already match the card — nothing to do.");

    if (flash && changed)
    {
        Console.WriteLine("[SOUNDS] flashing the dome with the new envelopes...");
        return await CmdUpload("dome", null);
    }
    if (flash) Console.WriteLine("[SOUNDS] no changes — dome not reflashed.");
    else if (changed) Console.WriteLine("[SOUNDS] flash it in with: bb8 upload dome   (or rerun with --flash)");
    return 0;
}

// ------------------------------------------------------------------
//  GitHub update check
//  The firmware lives in this repo, so "new firmware on GitHub" means new
//  commits on this branch's upstream. Before build/upload/deploy (always)
//  and before any other command (at most once every 4 h) bb8 fetches,
//  fast-forwards when that is safe, asks bb8.cmd to rebuild itself when
//  tools/ changed, and names the boards whose firmware moved.
//    bb8 update            check + pull now
//    bb8 update --flash    ...then reflash every plugged-in board whose
//                          running firmware is older than its sketch
//    --no-update / BB8_NO_UPDATE=1   skip the check
// ------------------------------------------------------------------
async Task<int> AutoUpdateCheck(string cmd)
{
    bool always = cmd is "build" or "upload" or "deploy";
    var stamp = Path.Combine(config.BuildRoot, ".update-check");
    if (!always && File.Exists(stamp) && DateTime.Now - File.GetLastWriteTime(stamp) < TimeSpan.FromHours(4))
        return 0;
    var r = await UpdateFromGitHub(explicitRun: false);
    return r.Applied && r.ToolChanged ? REBUILD_EXIT : 0;
}

async Task<int> CmdUpdate(bool flash)
{
    var r = Flag("--no-update") ? new UpdateResult(false, false, new()) : await UpdateFromGitHub(explicitRun: true);
    if (r.Applied && r.ToolChanged) return REBUILD_EXIT;   // bb8.cmd rebuilds, then re-runs "update [--flash] --no-update"
    if (!flash) return 0;

    // Flash whatever is plugged in AND stale. Stale is judged from the board's
    // own banner — the git hash it was built from vs. commits to its sketch
    // since — so a board that already runs the latest source is left alone.
    Console.WriteLine();
    int flashed = 0, failed = 0;
    foreach (var t in config.Targets)
    {
        var candidates = DetectPorts().Where(p => GuessTargets(p).Contains(t.Name)).ToList();
        if (candidates.Count == 0) { Console.WriteLine($"[FLASH] {t.Name,-6} not plugged in — skipped."); continue; }
        BannerStamp? found = null; string? port = null;
        foreach (var c in candidates)
        {
            var s = await ReadBannerStamp(t, c.Port, IsNativeUsb(t), quiet: true);
            if (s.Raw.Length == 0) continue;
            bool byBanner = t.BannerMatch is not null && s.Raw.Contains(t.BannerMatch, StringComparison.OrdinalIgnoreCase);
            bool byRev = t.RevMatch is not null && s.Rev is not null && s.Rev.EndsWith(t.RevMatch, StringComparison.OrdinalIgnoreCase);
            if (!byBanner && !byRev) continue;
            found = s; port = c.Port; break;
        }
        if (found is null || port is null)
        {
            Console.WriteLine($"[FLASH] {t.Name,-6} no board answering as '{t.Name}' on {string.Join("/", candidates.Select(c => c.Port))} — skipped.");
            continue;
        }
        var reason = StaleReason(t, found);
        if (reason is null)
        {
            Console.WriteLine($"\u001b[32m[FLASH] {t.Name,-6} {port}: build {found.Build} git {found.Git} — already current.\u001b[0m");
            continue;
        }
        Console.WriteLine($"\u001b[33m[FLASH] {t.Name,-6} {port}: {reason} — flashing.\u001b[0m");
        var rc = await CmdUpload(t.Name, port);
        if (rc == 0) flashed++; else failed++;
        Console.WriteLine();
    }
    Console.WriteLine(failed == 0
        ? $"\u001b[32m[FLASH] done — {flashed} board{(flashed == 1 ? "" : "s")} reflashed.\u001b[0m"
        : $"\u001b[31m[FLASH] {flashed} reflashed, {failed} failed — see above.\u001b[0m");
    return failed == 0 ? 0 : 1;
}

// null = the board runs the current source for its sketch; otherwise why not.
string? StaleReason(Bb8Target t, BannerStamp s)
{
    var repo = RepoRootDir();
    string G(string a) => $"-C \"{repo}\" {a}";
    var sketchPath = $"firmware/{t.Sketch}";
    if (s.Build < 0) return "running unstamped (pre-bb8) firmware";
    if (s.Git is null || s.Git is "none" or "nogit" or "unstamped") return $"build {s.Build} has no git stamp";
    var hash = s.Git.TrimEnd('+');
    if (Run("git", G($"cat-file -e {hash}^{{commit}}"), capture: true).rc != 0)
        return $"built from {hash}, which this checkout doesn't have";
    var since = Git(G($"rev-list --count {hash}..HEAD -- \"{sketchPath}\" \":(exclude){sketchPath}/BuildStamp.h\"")).Trim();
    if (int.TryParse(since, out var n) && n > 0)
        return $"build {s.Build} is {n} firmware commit{(n == 1 ? "" : "s")} behind ({hash} -> {Git(G("rev-parse --short HEAD")).Trim()})";
    var dirty = Git(G($"status --porcelain -- \"{sketchPath}\"")).Split('\n', StringSplitOptions.RemoveEmptyEntries)
        .Select(l => l.Length > 3 ? l[3..].Trim() : "").Where(f => !f.EndsWith("BuildStamp.h")).ToList();
    if (dirty.Count > 0) return $"uncommitted local changes in {sketchPath} ({dirty.Count} file{(dirty.Count == 1 ? "" : "s")})";
    return null;
}

async Task<UpdateResult> UpdateFromGitHub(bool explicitRun)
{
    var none = new UpdateResult(false, false, new());
    var repo = RepoRootDir();
    if (!Directory.Exists(Path.Combine(repo, ".git")))
    {
        if (explicitRun) Fail("This bb8 is not running from a git checkout — nothing to update from.");
        return none;
    }
    string G(string a) => $"-C \"{repo}\" {a}";
    void Note(string s) => Console.WriteLine(explicitRun ? s : $"\u001b[90m{s}\u001b[0m");

    // 1. fetch — bounded, the bench is often offline, and never prompt for credentials
    var (frc, _, ferr) = RunTimed("git", G("fetch --quiet origin"), 15000,
        new Dictionary<string, string> { ["GIT_TERMINAL_PROMPT"] = "0" });
    if (frc != 0)
    {
        Note($"[UPDATE] GitHub unreachable ({FirstLineOf(ferr, "timed out")}) — using local firmware.");
        return none;
    }
    try
    {
        Directory.CreateDirectory(config.BuildRoot);
        File.WriteAllText(Path.Combine(config.BuildRoot, ".update-check"), DateTime.Now.ToString("s"));
    }
    catch { }

    // 2. where this checkout stands vs. its upstream
    var branch = Git(G("rev-parse --abbrev-ref HEAD")).Trim();
    var upstream = Git(G("rev-parse --abbrev-ref --symbolic-full-name @{u}")).Trim();
    if (upstream.Length == 0) upstream = $"origin/{branch}";
    if (Run("git", G($"rev-parse --verify --quiet {upstream}"), capture: true).rc != 0)
    {
        Note($"[UPDATE] branch '{branch}' has no counterpart on GitHub — nothing to compare.");
        return none;
    }
    int behind = int.TryParse(Git(G($"rev-list --count HEAD..{upstream}")).Trim(), out var b) ? b : 0;
    int ahead  = int.TryParse(Git(G($"rev-list --count {upstream}..HEAD")).Trim(), out var a) ? a : 0;
    var head = Git(G("rev-parse --short HEAD")).Trim();
    if (behind == 0)
    {
        if (explicitRun || ahead > 0)
            Note($"[UPDATE] {branch} @ {head} is current with GitHub" +
                 (ahead > 0 ? $" ({ahead} local commit{(ahead == 1 ? "" : "s")} not pushed)." : "."));
        return none;
    }

    // 3. what is coming
    var files = Git(G($"diff --name-only HEAD {upstream}"))
        .Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(f => f.Trim()).ToList();
    var changedTargets = config.Targets
        .Where(t => files.Any(f => f.StartsWith($"firmware/{t.Sketch}/", StringComparison.OrdinalIgnoreCase)))
        .Select(t => t.Name).ToList();
    bool toolChanged = files.Any(f => f.StartsWith("tools/Bb8Commander/", StringComparison.OrdinalIgnoreCase)
                                   || f.Equals("install.ps1", StringComparison.OrdinalIgnoreCase));
    bool targetsChanged = files.Any(f => f.Equals("targets.json", StringComparison.OrdinalIgnoreCase));

    Console.WriteLine($"\u001b[36m[UPDATE] GitHub has {behind} new commit{(behind == 1 ? "" : "s")} on {branch}:\u001b[0m");
    var log = Git(G($"log --oneline --no-decorate HEAD..{upstream}")).Split('\n', StringSplitOptions.RemoveEmptyEntries);
    foreach (var l in log.Take(12)) Console.WriteLine($"         {l.Trim()}");
    if (log.Length > 12) Console.WriteLine($"         ... and {log.Length - 12} more");

    if (ahead > 0)
    {
        Console.WriteLine($"\u001b[33m[UPDATE] you also have {ahead} local commit{(ahead == 1 ? "" : "s")} GitHub doesn't — not auto-merging.\u001b[0m");
        Console.WriteLine($"         Reconcile by hand:  git -C \"{repo}\" pull --rebase");
        return new UpdateResult(false, toolChanged, changedTargets);
    }

    // 4. fast-forward. versions.json and BuildStamp.h are rewritten by every
    //    upload, so they are nearly always dirty; both are regenerated, so set
    //    them aside (keeping the higher build counters) instead of letting
    //    them block the merge. Anything else dirty makes git refuse — and we
    //    leave it at that, your edits are never touched.
    var localVersions = ReadVersions();
    var generated = Git(G("status --porcelain")).Split('\n', StringSplitOptions.RemoveEmptyEntries)
        .Select(l => l.Length > 3 ? l[3..].Trim() : "")
        .Where(f => f.Equals("versions.json", StringComparison.OrdinalIgnoreCase)
                 || (f.StartsWith("firmware/", StringComparison.OrdinalIgnoreCase) && f.EndsWith("/BuildStamp.h")))
        .ToList();
    if (generated.Count > 0)
        Run("git", G("checkout -- " + string.Join(' ', generated.Select(f => $"\"{f}\""))), capture: true);

    var (mrc, _, merr) = Run("git", G($"merge --ff-only {upstream}"), capture: true);

    var merged = ReadVersions();                      // never let a board's build number go backwards
    foreach (var kv in localVersions)
        if (!merged.TryGetValue(kv.Key, out var v) || v < kv.Value) merged[kv.Key] = kv.Value;
    WriteVersions(merged);

    if (mrc != 0)
    {
        Console.WriteLine($"\u001b[33m[UPDATE] could not fast-forward: {FirstLineOf(merr, "git refused")}\u001b[0m");
        Console.WriteLine($"         Your local edits are untouched. Commit or stash them, then:  git -C \"{repo}\" pull --ff-only");
        return new UpdateResult(false, toolChanged, changedTargets);
    }
    var newHead = Git(G("rev-parse --short HEAD")).Trim();
    Console.WriteLine($"\u001b[32m[UPDATE] {branch}: {head} -> {newHead}  (fast-forward, {behind} commit{(behind == 1 ? "" : "s")})\u001b[0m");

    if (targetsChanged)
    {
        config = JsonSerializer.Deserialize<Bb8Config>(File.ReadAllText(configPath!), JsonCtx.Default.Bb8Config)!;
        Console.WriteLine("[UPDATE] targets.json changed — reloaded.");
    }
    if (changedTargets.Count > 0)
        Console.WriteLine($"\u001b[33m[UPDATE] new firmware for: {string.Join(", ", changedTargets)}   ->   " +
                          string.Join("   ", changedTargets.Select(n => $"bb8 upload {n}")) +
                          "   (or: bb8 update --flash)\u001b[0m");
    if (toolChanged)
        Console.WriteLine("[UPDATE] bb8 itself changed — rebuilding from source, then continuing...");
    return new UpdateResult(true, toolChanged, changedTargets);
}

string Git(string arguments)
{
    var (rc, so, _) = Run("git", arguments, capture: true);
    return rc == 0 ? so : "";
}

static string FirstLineOf(string s, string fallback)
{
    var line = s.Split('\n').Select(l => l.Trim()).FirstOrDefault(l => l.Length > 0);
    return string.IsNullOrEmpty(line) ? fallback : line;
}

bool IsNativeUsb(Bb8Target t) => t.Fqbn.Contains(":avr:") || t.Fqbn.Contains(":samd:");

(int rc, string stdout, string stderr) RunTimed(string file, string arguments, int timeoutMs, Dictionary<string, string>? env = null)
{
    var psi = new ProcessStartInfo(file, arguments)
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        RedirectStandardInput = true,
        UseShellExecute = false
    };
    if (env is not null) foreach (var kv in env) psi.Environment[kv.Key] = kv.Value;
    try
    {
        using var p = Process.Start(psi)!;
        p.StandardInput.Close();
        var so = p.StandardOutput.ReadToEndAsync();
        var se = p.StandardError.ReadToEndAsync();
        if (!p.WaitForExit(timeoutMs))
        {
            try { p.Kill(entireProcessTree: true); } catch { }
            return (-1, "", $"no answer in {timeoutMs / 1000} s");
        }
        return (p.ExitCode, so.Result, se.Result);
    }
    catch (Exception ex) { return (-1, "", ex.Message); }
}

// ------------------------------------------------------------------
//  Build stamping — versions.json counter + BuildStamp.h generation
// ------------------------------------------------------------------
string RepoRootDir() => Path.GetDirectoryName(Path.GetFullPath(configPath!))!;

int BumpBuild(string target)
{
    var dict = ReadVersions();
    dict[target] = dict.TryGetValue(target, out var n) ? n + 1 : 1;
    WriteVersions(dict);
    return dict[target];
}

SortedDictionary<string, int> ReadVersions()
{
    var path = Path.Combine(RepoRootDir(), "versions.json");
    var dict = new SortedDictionary<string, int>(StringComparer.OrdinalIgnoreCase);
    if (File.Exists(path))
    {
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            foreach (var p in doc.RootElement.EnumerateObject())
                dict[p.Name] = p.Value.GetInt32();
        }
        catch (JsonException) { }
    }
    return dict;
}

void WriteVersions(SortedDictionary<string, int> dict)
{
    var path = Path.Combine(RepoRootDir(), "versions.json");
    var sb = new StringBuilder("{\n");
    sb.AppendJoin(",\n", dict.Select(kv => $"  \"{kv.Key}\": {kv.Value}"));
    sb.Append("\n}\n");
    var text = sb.ToString();
    if (!File.Exists(path) || File.ReadAllText(path).Replace("\r\n", "\n") != text) File.WriteAllText(path, text);
}

string GitStamp()
{
    var (rc, so, _) = Run("git", $"-C \"{RepoRootDir()}\" rev-parse --short HEAD", capture: true);
    if (rc != 0) return "nogit";
    var hash = so.Trim();
    var (rc2, so2, _) = Run("git", $"-C \"{RepoRootDir()}\" status --porcelain", capture: true);
    if (rc2 == 0 && so2.Trim().Length > 0) hash += "+";   // '+' = uncommitted changes at flash time
    return hash;
}

void WriteBuildStamp(Bb8Target t, int build)
{
    var git = GitStamp();
    var date = DateTime.Now.ToString("yyyy-MM-dd HH:mm");
    var p = Path.Combine(config.SketchRoot, t.Sketch, "BuildStamp.h");
    File.WriteAllText(p,
        "#pragma once\n" +
        "// Auto-generated by 'bb8 upload' before each flash - do not edit.\n" +
        $"#define BB8_BUILD_NUM  {build}\n" +
        $"#define BB8_BUILD_DATE \"{date}\"\n" +
        $"#define BB8_BUILD_GIT  \"{git}\"\n");
    // Touch the main .ino so arduino-cli recompiles the sketch unit (a header-only
    // BuildStamp change was observed to leave a stale build number in the binary).
    var ino = Path.Combine(config.SketchRoot, t.Sketch, t.Sketch + ".ino");
    if (File.Exists(ino)) File.SetLastWriteTimeUtc(ino, DateTime.UtcNow);
    Console.WriteLine($"\u001b[36m[STAMP] {t.Name} build {build} · {date} · git {git}\u001b[0m");
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
record BannerStamp(int Build, string? Git, string? Rev, string Raw);
record UpdateResult(bool Applied, bool ToolChanged, List<string> ChangedTargets);

class Channel(string label, string portName, int baud, string color, List<string> connectCmds)
{
    public string Label { get; } = label;
    public string PortName { get; } = portName;
    public int Baud { get; } = baud;
    public string Color { get; } = color;
    public List<string> ConnectCmds { get; } = connectCmds;
    public int ConnectCmdsSent { get; set; }
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
            ConnectCmdsSent = 0;
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
        Emit($"{DIM}type + Enter to send · Up/Down history · Tab switch board · q+Enter / Esc / Ctrl+C exit{RESET}");
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
            // Connect handshake: ask the board to identify itself shortly
            // after attaching (UART-bridged ESP32s can't detect a monitor on
            // their own). Commands come from targets.json connectCommands —
            // e.g. the drive answers version + cfg show + pid show.
            if (ch.ConnectCmdsSent < ch.ConnectCmds.Count &&
                (DateTime.Now - ch.OpenedAt).TotalMilliseconds > 700 + 400 * ch.ConnectCmdsSent)
            {
                var cmd = ch.ConnectCmds[ch.ConnectCmdsSent++];
                try { ch.Port.WriteLine(cmd); } catch (IOException) { }
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
                    var low = cmd.Trim().ToLowerInvariant();
                    if (low is "q" or "quit" or "exit")
                    {
                        _quit = true;
                        break;
                    }
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

record Transient(double Peak, int Overshoots, double SettleT, double TailSigma, double SatPct);

// Serial session for the tuners: owns the port, pumps lines, parses
// telemetry (lines starting with "t:"), logs everything to CSV.
class TuneLink : IDisposable
{
    readonly SerialPort _sp;
    readonly StringBuilder _buf = new();
    StreamWriter? _log;
    public bool Quit;

    public TuneLink(string port, int baud)
    {
        _sp = new SerialPort(port, baud) { ReadTimeout = 50, NewLine = "\n", DtrEnable = true, RtsEnable = true };
        _sp.Open();
    }

    public void OpenLog(string path)
    {
        _log = new StreamWriter(path, append: false) { AutoFlush = true };
        _log.WriteLine("time,board,line");
    }

    public void LogNote(string note) => _log?.WriteLine($"{DateTime.Now:HH:mm:ss.fff},tune,{note}");

    public void Send(string cmd)
    {
        try { _sp.WriteLine(cmd); } catch (Exception ex) { Console.WriteLine($"\u001b[31m[TUNE] write failed: {ex.Message}\u001b[0m"); }
        Console.WriteLine($"\u001b[35m>> {cmd}\u001b[0m");
        _log?.WriteLine($"{DateTime.Now:HH:mm:ss.fff},tune,>> {cmd}");
    }

    public List<string> Pump(int ms, Action<Dictionary<string, double>>? onTlm)
    {
        var lines = new List<string>();
        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < ms && !Quit)
        {
            string chunk = "";
            try { chunk = _sp.ReadExisting(); } catch (TimeoutException) { } catch (IOException) { Quit = true; break; }
            foreach (var c in chunk)
            {
                if (c != '\n') { if (_buf.Length < 512) _buf.Append(c); continue; }
                var line = _buf.ToString().TrimEnd('\r');
                _buf.Clear();
                if (line.Length == 0) continue;
                lines.Add(line);
                _log?.WriteLine($"{DateTime.Now:HH:mm:ss.fff},board,{(line.Contains(',') ? "\"" + line.Replace("\"", "\"\"") + "\"" : line)}");
                if (line.StartsWith("t:") && onTlm is not null)
                {
                    var d = new Dictionary<string, double>();
                    foreach (var kv in line.Split(','))
                    {
                        var i = kv.IndexOf(':');
                        if (i > 0 && double.TryParse(kv[(i + 1)..], System.Globalization.CultureInfo.InvariantCulture, out var v))
                            d[kv[..i]] = v;
                    }
                    if (d.Count > 2) onTlm(d);
                }
            }
            Thread.Sleep(5);
        }
        return lines;
    }

    public void Dispose()
    {
        try { _sp.Close(); } catch { }
        _sp.Dispose();
        _log?.Dispose();
    }
}

public static class VtConsole
{
    [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int nStdHandle);
    [DllImport("kernel32.dll")] static extern bool GetConsoleMode(IntPtr h, out uint mode);
    [DllImport("kernel32.dll")] static extern bool SetConsoleMode(IntPtr h, uint mode);

    public static void Enable()
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return;
        try
        {
            var h = GetStdHandle(-11); // STD_OUTPUT_HANDLE
            if (GetConsoleMode(h, out var m)) SetConsoleMode(h, m | 0x0004);
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
    [JsonPropertyName("revMatch")] public string? RevMatch { get; set; }        // suffix of the revision field in "BOOT | <rev> | build N"
    [JsonPropertyName("connectCommands")] public List<string>? ConnectCommands { get; set; }
}

[JsonSerializable(typeof(Bb8Config))]
public partial class JsonCtx : JsonSerializerContext { }
