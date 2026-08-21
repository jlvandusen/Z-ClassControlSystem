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
        case "tune":     return await CmdTune(Arg(1), Opt("--port"));
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
          bb8 tune <s2s|drive> [--port COMx]      LIVE closed-loop gain tuner (rig)
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

    // Stamp this write: incrementing build number + date + git hash, baked
    // into the binary via BuildStamp.h so 'version' on the board matches
    // back to the exact code.
    var build = BumpBuild(t.Name);
    WriteBuildStamp(t, build);

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

// ------------------------------------------------------------------
//  TUNE — live closed-loop gain tuner
//  Owns the serial port, streams 100 Hz telemetry, measures each
//  nudge transient, adjusts PID gains over the wire, repeats until
//  the response is critically damped, then saves.
// ------------------------------------------------------------------
async Task<int> CmdTune(string? axis, string? portOpt)
{
    axis = axis?.ToLowerInvariant();
    if (axis is not ("s2s" or "drive"))
    {
        Fail("tune: axis must be 's2s' (roll) or 'drive' (pitch).");
        return 1;
    }
    bool isS2s = axis == "s2s";
    string chan = isS2s ? "roll" : "pitch";

    var t = ResolveTarget("drive");
    var port = portOpt ?? await AutoPort(t);
    if (port is null) { Fail("No drive port found. Close any monitor and plug the drive in."); return 1; }

    Console.WriteLine($"""
        [36m=== bb8 tune {axis} — live closed-loop tuner ===[0m
        Droid on the ROLLERS, drive enabled (CIRCLE), autoBalance ON (CROSS).
        When prompted: nudge the top ~5 deg {(isS2s ? "SIDEWAYS" : "FORWARD")} and LET GO.
        Ctrl+C aborts (gains stay whatever was last set, not saved).
        """);

    using var sp = new SerialPort(port, t.Baud) { ReadTimeout = 50, NewLine = "\n", DtrEnable = true, RtsEnable = true };
    try { sp.Open(); } catch (Exception ex) { Fail($"Cannot open {port}: {ex.Message} (close the monitor?)"); return 1; }

    var lineBuf = new StringBuilder();
    var quit = false;
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; quit = true; };

    void Send(string cmd)
    {
        sp.WriteLine(cmd);
        Console.WriteLine($"[35m>> {cmd}[0m");
    }

    // Pump serial; invoke onTlm per telemetry sample, collect raw lines
    List<string> Pump(int ms, Action<Dictionary<string, double>>? onTlm)
    {
        var lines = new List<string>();
        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < ms && !quit)
        {
            string chunk = "";
            try { chunk = sp.ReadExisting(); } catch (TimeoutException) { }
            foreach (var c in chunk)
            {
                if (c != '\n') { if (lineBuf.Length < 512) lineBuf.Append(c); continue; }
                var line = lineBuf.ToString().TrimEnd('\r');
                lineBuf.Clear();
                if (line.Length == 0) continue;
                lines.Add(line);
                if (line.Contains("pitch:") && line.Contains("roll:") && onTlm is not null)
                {
                    var d = new Dictionary<string, double>();
                    foreach (var kv in line.Split(','))
                    {
                        var i = kv.IndexOf(':');
                        if (i > 0 && double.TryParse(kv[(i + 1)..], System.Globalization.CultureInfo.InvariantCulture, out var v))
                            d[kv[..i]] = v;
                    }
                    if (d.ContainsKey(chan)) onTlm(d);
                }
            }
            Thread.Sleep(5);
        }
        return lines;
    }

    // ---- session setup ----
    Send("telemetry fast");
    double kp = 0, ki = 0, kd = 0;
    var pidLines = Pump(700, null);
    Send("pid show");
    foreach (var l in Pump(900, null))
    {
        // [PID] Drive: Kp=12.00 Ki=6.00 Kd=0.50 | S2S: Kp=30.00 Ki=10.00 Kd=1.00
        var m = System.Text.RegularExpressions.Regex.Match(l,
            isS2s ? @"S2S: Kp=([\d.]+) Ki=([\d.]+) Kd=([\d.]+)"
                  : @"Drive: Kp=([\d.]+) Ki=([\d.]+) Kd=([\d.]+)");
        if (m.Success)
        {
            kp = double.Parse(m.Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture);
            ki = double.Parse(m.Groups[2].Value, System.Globalization.CultureInfo.InvariantCulture);
            kd = double.Parse(m.Groups[3].Value, System.Globalization.CultureInfo.InvariantCulture);
        }
    }
    if (kp == 0) { Fail("Could not read current PID gains (is this the drive board? monitor closed?)"); return 1; }
    Console.WriteLine($"[36m[TUNE] starting gains: Kp={kp:F1} Ki={ki:F1} Kd={kd:F2}[0m");

    // Wait for en=1 & bal=1
    bool ready = false; var readyDeadline = DateTime.Now.AddSeconds(60);
    Console.WriteLine("[TUNE] waiting for drive ENABLED + autoBalance ON (CIRCLE then CROSS)...");
    while (!ready && DateTime.Now < readyDeadline && !quit)
        Pump(400, d => { if (d.GetValueOrDefault("en") == 1 && d.GetValueOrDefault("bal") == 1) ready = true; });
    if (!ready) { Fail("Timed out waiting for drive+balance to be enabled."); return 1; }
    Console.WriteLine("[32m[TUNE] balance active — beginning cycles[0m");

    void SetGains()
    {
        Send($"pid set {axis} kp {kp:F1}"); Pump(250, null);
        Send($"pid set {axis} ki {ki:F1}"); Pump(250, null);
        Send($"pid set {axis} kd {kd:F2}"); Pump(250, null);
    }

    // ---- tuning cycles ----
    const int MAX_CYCLES = 7;
    int goodStreak = 0;
    for (int cycle = 1; cycle <= MAX_CYCLES && !quit; cycle++)
    {
        Console.WriteLine($"\n[33m[TUNE {cycle}/{MAX_CYCLES}] >>> NUDGE the droid ~5 deg and LET GO <<<[0m");

        // wait for the nudge: |chan| exceeding 3 deg
        double baseline = 0; int baseN = 0;
        Pump(800, d => { baseline += d[chan]; baseN++; });
        if (baseN > 0) baseline /= baseN;

        bool kicked = false; var kickDeadline = DateTime.Now.AddSeconds(20);
        while (!kicked && DateTime.Now < kickDeadline && !quit)
            Pump(100, d => { if (Math.Abs(d[chan] - baseline) > 3.0) kicked = true; });
        if (!kicked) { Console.WriteLine("[TUNE] no nudge detected in 20 s — skipping cycle"); continue; }

        // capture the transient: 6 s
        var samp = new List<double>(700);
        var satC = 0; var total = 0;
        string pwmKey = isS2s ? "s2s" : "drv";
        Pump(6000, d =>
        {
            samp.Add(d[chan] - baseline);
            total++;
            if (Math.Abs(d.GetValueOrDefault(pwmKey)) >= 250) satC++;
        });
        if (samp.Count < 100) { Console.WriteLine("[TUNE] too little data — retry"); cycle--; continue; }

        // metrics: peak, overshoot count (sign reversals of extremes), settle time, tail sigma
        double peak = samp.Take(100).Select(Math.Abs).Max();
        int overshoots = 0; int sign = 0;
        foreach (var v in samp)
        {
            if (sign >= 0 && v < -Math.Max(0.8, peak * 0.15)) { if (sign > 0) overshoots++; sign = -1; }
            else if (sign <= 0 && v > Math.Max(0.8, peak * 0.15)) { if (sign < 0) overshoots++; sign = 1; }
        }
        double settleT = 6.0;
        for (int i = 0; i < samp.Count - 50; i++)
        {
            if (samp.Skip(i).Take(50).All(v => Math.Abs(v) < 1.2)) { settleT = i / 100.0; break; }
        }
        var tail = samp.Skip((int)(samp.Count * 0.7)).ToList();
        double tailMean = tail.Average();
        double tailSigma = Math.Sqrt(tail.Select(v => (v - tailMean) * (v - tailMean)).Average());
        double satPct = 100.0 * satC / Math.Max(1, total);

        Console.WriteLine($"[TUNE] peak={peak:F1} deg overshoots={overshoots} settle={settleT:F1}s tailSigma={tailSigma:F2} sat={satPct:F0}%");

        // classify + adjust
        string verdict;
        if (tailSigma > 2.0 || settleT >= 5.9)
        {
            verdict = "STILL OSCILLATING"; kp *= 0.65; kd *= 1.15; ki = Math.Min(ki, 2); goodStreak = 0;
        }
        else if (overshoots >= 3)
        {
            verdict = "RINGING"; kp *= 0.8; kd *= 1.2; goodStreak = 0;
        }
        else if (overshoots <= 1 && settleT < 2.0)
        {
            goodStreak++;
            if (goodStreak == 1 && ki < 2) { verdict = "GOOD — adding centering Ki"; ki = 3; }
            else if (goodStreak >= 2) { verdict = "GOOD — confirmed"; Console.WriteLine($"[32m[TUNE] {verdict}[0m"); break; }
            else verdict = "GOOD — confirming";
        }
        else if (overshoots == 0 && settleT > 3.0)
        {
            verdict = "SLUGGISH"; kp *= 1.25; goodStreak = 0;
        }
        else
        {
            verdict = "ACCEPTABLE — small refine"; kd *= 1.1; goodStreak = 0;
        }

        kp = Math.Clamp(kp, 1, isS2s ? 200 : 100);
        kd = Math.Clamp(kd, 0, 20);
        ki = Math.Clamp(ki, 0, 100);
        Console.WriteLine($"[36m[TUNE] {verdict} -> Kp={kp:F1} Ki={ki:F1} Kd={kd:F2}[0m");
        SetGains();
    }

    if (!quit)
    {
        Send("pid save");
        Pump(600, null);
        Console.WriteLine($"[32m[TUNE] DONE — saved {axis}: Kp={kp:F1} Ki={ki:F1} Kd={kd:F2}[0m");
    }
    else Console.WriteLine("[TUNE] aborted — gains left as last set, NOT saved.");
    Send("telemetry off");
    Pump(300, null);
    return 0;
}

// ------------------------------------------------------------------
//  Build stamping — versions.json counter + BuildStamp.h generation
// ------------------------------------------------------------------
string RepoRootDir() => Path.GetDirectoryName(Path.GetFullPath(configPath!))!;

int BumpBuild(string target)
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
    dict[target] = dict.TryGetValue(target, out var n) ? n + 1 : 1;
    var sb = new StringBuilder("{\n");
    sb.AppendJoin(",\n", dict.Select(kv => $"  \"{kv.Key}\": {kv.Value}"));
    sb.Append("\n}\n");
    File.WriteAllText(path, sb.ToString());
    return dict[target];
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
    Console.WriteLine($"[36m[STAMP] {t.Name} build {build} · {date} · git {git}[0m");
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
    [JsonPropertyName("connectCommands")] public List<string>? ConnectCommands { get; set; }
}

[JsonSerializable(typeof(Bb8Config))]
public partial class JsonCtx : JsonSerializerContext { }
