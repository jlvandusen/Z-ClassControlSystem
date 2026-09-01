// OTA sender: streams a drive app image THROUGH the dome (USB serial in,
// ESP-NOW out). Control commands ride the normal console tunnel; data rides
// "OTAD <seq> <base64>" lines the dome converts to 192-byte binary packets.
// The drive acks every chunk on its mirrored console ("[OTAACK] n"), so this
// is a simple stop-and-wait sender — serial time dominates anyway (~2-3 min
// for a 1.2 MB image at 115200).
// Requirements on the droid: drive DISABLED, a gamepad CONNECTED (the dome
// cannot reach a drive that is mid-BT-scan).

using System.IO.Ports;
using System.Text;
using System.Text.RegularExpressions;

static class OtaSender
{
    const int CHUNK = 192;

    public static int Send(string port, int baud, byte[] image)
    {
        using var sp = Open(port, baud);
        if (sp is null) return 1;
        var buf = new StringBuilder();

        // handshake — the begin command tunnels to the drive's console
        string? ready = null;
        for (int t = 0; t < 3 && ready is null; t++)
        {
            try { sp.DiscardInBuffer(); } catch (Exception) { }
            buf.Clear();
            sp.WriteLine($"ota begin {image.Length}");
            ready = WaitFor(sp, buf, l => l.Contains("[OTA] READY") || l.Contains("[OTA] FAIL"), 6000);
        }
        if (ready is null) { Console.WriteLine("\u001b[31m[OTA] no answer to 'ota begin' — is the pad connected and the drive powered?\u001b[0m"); return 1; }
        if (ready.Contains("FAIL")) { Console.WriteLine($"\u001b[31m{ready.Trim()}\u001b[0m"); return 1; }

        int total = (image.Length + CHUNK - 1) / CHUNK;
        int seq = 0, lastPct = -1;
        var sw = System.Diagnostics.Stopwatch.StartNew();
        while (seq < total)
        {
            int off = seq * CHUNK, n = Math.Min(CHUNK, image.Length - off);
            var b64 = Convert.ToBase64String(image, off, n);
            int next = -1;
            for (int attempt = 0; attempt < 8 && next < 0; attempt++)
            {
                sp.WriteLine($"OTAD {seq} {b64}");
                var resp = WaitFor(sp, buf,
                    l => l.Contains("[OTAACK] ") || l.Contains("[OTAERR]") || l.Contains("[OTA] FAIL"), 2500);
                if (resp is null) continue;
                if (resp.Contains("[OTA] FAIL")) { Console.WriteLine($"\n\u001b[31m{resp.Trim()}\u001b[0m"); return 1; }
                var ack = Regex.Match(resp, @"\[OTAACK\] (\d+)");
                if (ack.Success && int.Parse(ack.Groups[1].Value) == seq) { next = seq + 1; break; }
                var want = Regex.Match(resp, @"\[OTAERR\] want (\d+)");
                if (want.Success) { next = int.Parse(want.Groups[1].Value); break; }
            }
            if (next < 0)
            {
                Console.WriteLine($"\n\u001b[31m[OTA] chunk {seq} never acked (8 tries) — link down? 'bb8 monitor ball' to inspect.\u001b[0m");
                return 1;
            }
            seq = next;
            int pct = (int)((long)seq * 100 / total);
            if (pct != lastPct)
            {
                var rate = seq * CHUNK / Math.Max(1.0, sw.Elapsed.TotalSeconds) / 1024.0;
                Console.Write($"\r[OTA] {pct}%  ({seq}/{total} chunks, {rate:F1} KB/s)   ");
                lastPct = pct;
            }
        }
        Console.WriteLine();

        sp.WriteLine("ota end");
        var done = WaitFor(sp, buf, l => l.Contains("[OTA] OK") || l.Contains("[OTA] FAIL"), 20000);
        if (done is null || done.Contains("FAIL"))
        {
            Console.WriteLine($"\u001b[31m[OTA] finalize failed: {done?.Trim() ?? "no answer"}\u001b[0m");
            return 1;
        }
        Console.WriteLine($"\u001b[32m{done.Trim()}\u001b[0m");
        return 0;
    }

    // After the reboot, ask the drive for its banner through the tunnel.
    // ("ver", because the dome answers "version" itself.)
    public static int VerifyBuild(string port, int baud, int timeoutMs)
    {
        using var sp = Open(port, baud);
        if (sp is null) return -1;
        var buf = new StringBuilder();
        var deadline = DateTime.Now.AddMilliseconds(timeoutMs);
        while (DateTime.Now < deadline)
        {
            sp.WriteLine("ver");
            var line = WaitFor(sp, buf, l => Regex.IsMatch(l, @"VERSION \| .* \| build \d+"), 2500);
            if (line is not null)
            {
                var m = Regex.Match(line, @"build (\d+)");
                if (m.Success) return int.Parse(m.Groups[1].Value);
            }
        }
        return -1;
    }

    static SerialPort? Open(string port, int baud)
    {
        var sp = new SerialPort(port, baud) { ReadTimeout = 50, NewLine = "\n", DtrEnable = true, RtsEnable = true };
        try { sp.Open(); return sp; }
        catch (Exception ex)
        {
            Console.WriteLine($"\u001b[31m[OTA] could not open {port}: {ex.Message}\u001b[0m");
            sp.Dispose();
            return null;
        }
    }

    // Pump the port, return the first full line the predicate matches.
    static string? WaitFor(SerialPort sp, StringBuilder buf, Func<string, bool> match, int timeoutMs)
    {
        var deadline = DateTime.Now.AddMilliseconds(timeoutMs);
        while (DateTime.Now < deadline)
        {
            string chunk;
            try { chunk = sp.ReadExisting(); }
            catch (Exception) { return null; }
            foreach (var c in chunk)
            {
                if (c == '\n')
                {
                    var line = buf.ToString().TrimEnd('\r');
                    buf.Clear();
                    if (match(line)) return line;
                }
                else if (buf.Length < 512) buf.Append(c);
            }
            Thread.Sleep(5);
        }
        return null;
    }
}
