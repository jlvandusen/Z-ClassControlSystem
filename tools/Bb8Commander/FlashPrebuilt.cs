// Prebuilt-binary flashing without arduino-cli (the BASIC install path).
// Three methods, one per board family:
//   esptool - ESP32 drive/dome: bundled tools\flash\esptool.exe writes the four
//             images (bootloader/partitions/boot_app0/app) at their offsets.
//   avr109  - Feather 32u4 body: the Caterina bootloader speaks AVR109, a simple
//             serial protocol we implement natively (no avrdude needed).
//   uf2     - Trinket M0 imu: the UF2 bootloader is a USB drive (TRINKETBOOT);
//             we convert the .bin to .uf2 and copy it on.
// What to flash comes from binaries\<target>\flash.json, written by make-release.

using System.IO.Ports;
using System.Runtime.InteropServices;
using System.Text.Json;

// The Caterina BOOTLOADER's CDC port opens fine via raw CreateFile but .NET
// SerialPort.Open dies in its config IOCTLs ("device not functioning" —
// bench-proven 2026-09-05). So AVR109 talks through a bare Win32 handle;
// baud is advisory over USB CDC, and SetCommState failures are ignored.
sealed class RawSerial : IDisposable
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadFile(IntPtr h, byte[] buf, uint n, out uint read, IntPtr ovl);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WriteFile(IntPtr h, byte[] buf, uint n, out uint written, IntPtr ovl);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetCommTimeouts(IntPtr h, ref ComTimeouts t);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetCommState(IntPtr h, ref Dcb dcb);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool EscapeCommFunction(IntPtr h, uint func);

    [StructLayout(LayoutKind.Sequential)]
    struct ComTimeouts { public uint ReadInterval, ReadTotalMult, ReadTotalConst, WriteTotalMult, WriteTotalConst; }
    [StructLayout(LayoutKind.Sequential)]
    struct Dcb
    {
        public uint DCBlength, BaudRate, Flags;
        public ushort wReserved, XonLim, XoffLim;
        public byte ByteSize, Parity, StopBits;
        public sbyte XonChar, XoffChar, ErrorChar, EofChar, EvtChar;
        public ushort wReserved1;
    }

    IntPtr _h = new(-1);

    public bool Open(string port)
    {
        _h = CreateFile(@"\\.\" + port, 0xC0000000 /*GENERIC_RW*/, 0, IntPtr.Zero, 3 /*OPEN_EXISTING*/, 0, IntPtr.Zero);
        if (_h == new IntPtr(-1)) return false;
        var t = new ComTimeouts { ReadInterval = 0xFFFFFFFF, ReadTotalMult = 0xFFFFFFFF, ReadTotalConst = 200, WriteTotalConst = 2000 };
        SetCommTimeouts(_h, ref t);
        var dcb = new Dcb
        {
            DCBlength = (uint)Marshal.SizeOf<Dcb>(),
            BaudRate = 57600,
            ByteSize = 8,
            // fBinary | fDtrControl=ENABLE | fRtsControl=ENABLE
            Flags = 0x1 | 0x10 | 0x1000
        };
        SetCommState(_h, ref dcb);              // usbser may refuse — harmless over CDC
        EscapeCommFunction(_h, 5);              // SETDTR
        EscapeCommFunction(_h, 3);              // SETRTS
        return true;
    }

    public void Write(byte[] data)
    {
        if (!WriteFile(_h, data, (uint)data.Length, out var w, IntPtr.Zero) || w != data.Length)
            throw new IOException($"write failed (err {Marshal.GetLastWin32Error()})");
    }

    // Read exactly n bytes or throw after deadlineMs.
    public byte[] ReadExact(int n, int deadlineMs)
    {
        var outBuf = new byte[n];
        int got = 0;
        var deadline = DateTime.Now.AddMilliseconds(deadlineMs);
        var chunk = new byte[n];
        while (got < n)
        {
            if (DateTime.Now > deadline) throw new TimeoutException("bootloader went quiet");
            if (!ReadFile(_h, chunk, (uint)(n - got), out var r, IntPtr.Zero))
                throw new IOException($"read failed (err {Marshal.GetLastWin32Error()})");
            for (uint i = 0; i < r; i++) outBuf[got++] = chunk[i];
        }
        return outBuf;
    }

    public void Dispose()
    {
        if (_h != new IntPtr(-1)) { CloseHandle(_h); _h = new IntPtr(-1); }
    }
}

record FlashImage(string File, uint Offset);

record FlashManifest(string Method, int Build, int Baud, List<FlashImage> Images,
                     uint Base, uint FamilyId, string Volume,
                     string FlashMode, string FlashFreq)
{
    public static FlashManifest Load(string path)
    {
        using var doc = JsonDocument.Parse(System.IO.File.ReadAllText(path));
        var r = doc.RootElement;
        uint Hex(string name, uint dflt) =>
            r.TryGetProperty(name, out var e) ? Convert.ToUInt32(e.GetString()!, 16) : dflt;
        var images = new List<FlashImage>();
        if (r.TryGetProperty("images", out var arr))
            foreach (var e in arr.EnumerateArray())
                images.Add(new FlashImage(e.GetProperty("file").GetString()!,
                                          Convert.ToUInt32(e.GetProperty("offset").GetString()!, 16)));
        if (r.TryGetProperty("file", out var f))
            images.Add(new FlashImage(f.GetString()!, 0));
        return new FlashManifest(
            r.GetProperty("method").GetString()!,
            r.TryGetProperty("build", out var b) ? b.GetInt32() : -1,
            r.TryGetProperty("baud", out var bd) ? bd.GetInt32() : 921600,
            images,
            Hex("base", 0x2000),
            Hex("familyId", 0x68ED2B88),
            r.TryGetProperty("volume", out var v) ? v.GetString()! : "TRINKETBOOT",
            r.TryGetProperty("flashMode", out var fm) ? fm.GetString()! : "dio",
            r.TryGetProperty("flashFreq", out var ff) ? ff.GetString()! : "80m");
    }
}

static class PrebuiltFlash
{
    // ---------------- esptool (ESP32) ----------------
    // Mirrors what arduino-cli invokes. Mode/freq must be EXPLICIT so esptool
    // patches the bootloader header — "keep" left an unpatched header and the
    // board boot-looped (RTCWDT resets; bench-verified 2026-09-05).
    public static int Esp32(string esptool, string port, FlashManifest m, string dir)
    {
        var args = $"--chip esp32 --port {port} --baud {m.Baud} --before default_reset --after hard_reset " +
                   $"write_flash -z --flash_mode {m.FlashMode} --flash_freq {m.FlashFreq} --flash_size detect " +
                   string.Join(' ', m.Images.Select(i => $"0x{i.Offset:X} \"{Path.Combine(dir, i.File)}\""));
        Console.WriteLine($"\u001b[36m[FLASH] esptool write_flash ({m.Images.Count} images) -> {port}\u001b[0m");
        var psi = new System.Diagnostics.ProcessStartInfo(esptool, args) { UseShellExecute = false };
        try
        {
            using var p = System.Diagnostics.Process.Start(psi)!;
            p.WaitForExit();
            return p.ExitCode;
        }
        catch (Exception ex) { Console.WriteLine($"\u001b[31m[FLASH] could not run esptool: {ex.Message}\u001b[0m"); return 1; }
    }

    // ---------------- AVR109 (Caterina, Feather 32u4) ----------------
    // Bootloader lives at 0x7000; the app image must stay below it.
    public const int AVR_FLASH_APP_END = 0x7000;

    public static int Avr109(string bootPort, byte[] image, int length)
    {
        if (length > AVR_FLASH_APP_END)
        {
            Console.WriteLine($"\u001b[31m[FLASH] image is {length} bytes — larger than the 0x{AVR_FLASH_APP_END:X} app area. Refusing.\u001b[0m");
            return 1;
        }
        // Raw Win32 handle: .NET SerialPort's config IOCTLs fail against the
        // Caterina bootloader's CDC ("device not functioning") while a plain
        // CreateFile works every time — bench-proven 2026-09-05.
        using var sp = new RawSerial();
        bool opened = false;
        for (int i = 0; i < 12 && !opened; i++)
        {
            opened = sp.Open(bootPort);
            if (!opened) Thread.Sleep(250);
        }
        if (!opened)
        {
            Console.WriteLine($"\u001b[31m[FLASH] could not open the bootloader port {bootPort} inside its window.\u001b[0m");
            return 1;
        }
        try
        {
            string Expect(int n) => System.Text.Encoding.ASCII.GetString(sp.ReadExact(n, 3000));
            void Cmd(byte[] bytes) => sp.Write(bytes);

            Cmd("S"u8.ToArray());
            var id = Expect(7);
            Console.WriteLine($"\u001b[36m[FLASH] AVR109 bootloader '{id}' on {bootPort}\u001b[0m");

            Cmd("b"u8.ToArray());                        // block-mode buffer size
            var y = Expect(3);
            int bufSize = y[0] == 'Y' ? (y[1] << 8) | y[2] : 128;
            if (bufSize is < 16 or > 256) bufSize = 128;

            // Chip-erase the app section first — avrdude always does, and some
            // Caterina builds' block-write does NOT page-erase: writing over an
            // old image then just ANDs the bits (acked writes, corrupt app).
            Console.Write("[FLASH] erasing... ");
            Cmd("e"u8.ToArray());
            if (System.Text.Encoding.ASCII.GetString(sp.ReadExact(1, 15000)) != "\r")
                throw new IOException("erase not acknowledged");
            Console.WriteLine("done");

            Cmd(new byte[] { (byte)'A', 0, 0 });         // word address 0
            if (Expect(1) != "\r") throw new IOException("address set not acknowledged");

            int total = (length + bufSize - 1) / bufSize * bufSize;
            for (int off = 0; off < total; off += bufSize)
            {
                var block = new byte[bufSize];
                for (int i = 0; i < bufSize; i++)
                    block[i] = off + i < length ? image[off + i] : (byte)0xFF;
                Cmd(new byte[] { (byte)'B', (byte)(bufSize >> 8), (byte)(bufSize & 0xFF), (byte)'F' });
                Cmd(block);
                if (Expect(1) != "\r") throw new IOException($"write not acknowledged at 0x{off:X}");
                if (off % 4096 == 0) Console.Write($"\r[FLASH] {off * 100 / Math.Max(1, total)}% ");
            }
            Console.WriteLine($"\r[FLASH] 100% — {length} bytes written");

            // Read the whole image back BEFORE exiting the bootloader — a bad write
            // is recoverable here, unbootable after 'E' it is not. On mismatch,
            // print enough to diagnose (blank? shifted? garbage?).
            Cmd(new byte[] { (byte)'A', 0, 0 });
            if (Expect(1) != "\r") throw new IOException("verify address set not acknowledged");
            var rb = new byte[total];
            for (int off = 0; off < total; off += bufSize)
            {
                Cmd(new byte[] { (byte)'g', (byte)(bufSize >> 8), (byte)(bufSize & 0xFF), (byte)'F' });
                Array.Copy(sp.ReadExact(bufSize, 3000), 0, rb, off, bufSize);
                if (off % 4096 == 0) Console.Write($"\r[FLASH] verify {off * 100 / Math.Max(1, total)}% ");
            }
            var want = new byte[total];
            for (int i = 0; i < total; i++) want[i] = i < length ? image[i] : (byte)0xFF;
            int bad = 0, firstBad = -1;
            for (int i = 0; i < total; i++)
                if (rb[i] != want[i]) { bad++; if (firstBad < 0) firstBad = i; }
            if (bad > 0)
            {
                Console.WriteLine($"\r\u001b[31m[FLASH] VERIFY FAILED: {bad}/{total} bytes differ, first at 0x{firstBad:X}\u001b[0m");
                int b0 = Math.Max(0, (firstBad & ~15));
                string Hex(byte[] a, int o) => string.Join(" ", Enumerable.Range(o, Math.Min(16, total - o)).Select(k => a[k].ToString("X2")));
                Console.WriteLine($"[FLASH]  wrote@0x{b0:X4}: {Hex(want, b0)}");
                Console.WriteLine($"[FLASH]   read@0x{b0:X4}: {Hex(rb, b0)}");
                if (rb.Take(Math.Min(total, 512)).All(x => x == 0xFF))
                    Console.WriteLine("[FLASH]  read is blank 0xFF — erase worked but the writes never landed");
                int shift = -1;
                for (int s = 1; s <= 128 && shift < 0; s++)
                    if (total > s + 64 && Enumerable.Range(0, 64).All(k => rb[s + k] == want[k])) shift = s;
                if (shift > 0) Console.WriteLine($"[FLASH]  the image appears at +{shift} bytes — writes are SHIFTED");
                Console.WriteLine("[FLASH]  NOT exiting the bootloader — the board is still recoverable. Retry, or double-tap + 'bb8 upload body --port COMx'.");
                return 1;
            }
            Console.WriteLine("\r[FLASH] verify OK — image read back identical   ");

            Cmd("E"u8.ToArray());                        // exit bootloader -> app starts
            try { Expect(1); } catch (Exception) { }     // board may reset before acking
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"\u001b[31m[FLASH] AVR109 failed: {ex.Message}\u001b[0m");
            return 1;
        }
    }

    // Intel HEX -> flat image (0xFF-filled). Returns the byte count past the
    // highest address written.
    public static byte[] ParseIntelHex(string path, out int length)
    {
        var image = new byte[AVR_FLASH_APP_END + 0x1000];
        Array.Fill(image, (byte)0xFF);
        int max = 0, baseAddr = 0;
        foreach (var raw in System.IO.File.ReadLines(path))
        {
            var line = raw.Trim();
            if (line.Length < 11 || line[0] != ':') continue;
            byte B(int i) => Convert.ToByte(line.Substring(1 + i * 2, 2), 16);
            int count = B(0), addr = (B(1) << 8) | B(2), type = B(3);
            if (type == 1) break;                                   // EOF
            if (type == 2) { baseAddr = ((B(4) << 8) | B(5)) << 4; continue; }
            if (type == 4) { baseAddr = ((B(4) << 8) | B(5)) << 16; continue; }
            if (type != 0) continue;
            for (int i = 0; i < count; i++)
            {
                int a = baseAddr + addr + i;
                if (a >= image.Length) throw new IOException($"hex record beyond flash at 0x{a:X}");
                image[a] = B(4 + i);
                if (a + 1 > max) max = a + 1;
            }
        }
        length = max;
        return image;
    }

    // ---------------- UF2 (Trinket M0) ----------------
    public static byte[] BinToUf2(byte[] bin, uint baseAddr, uint familyId)
    {
        const int PAYLOAD = 256;
        int blocks = (bin.Length + PAYLOAD - 1) / PAYLOAD;
        var outBuf = new byte[blocks * 512];
        for (int b = 0; b < blocks; b++)
        {
            int o = b * 512;
            void W32(int off, uint v) => BitConverter.TryWriteBytes(outBuf.AsSpan(o + off, 4), v);
            W32(0, 0x0A324655);                          // "UF2\n"
            W32(4, 0x9E5D5157);
            W32(8, 0x00002000);                          // flag: familyID present
            W32(12, baseAddr + (uint)(b * PAYLOAD));     // target address
            W32(16, PAYLOAD);
            W32(20, (uint)b);
            W32(24, (uint)blocks);
            W32(28, familyId);
            int n = Math.Min(PAYLOAD, bin.Length - b * PAYLOAD);
            bin.AsSpan(b * PAYLOAD, n).CopyTo(outBuf.AsSpan(o + 32, n));
            W32(508, 0x0AB16F30);
        }
        return outBuf;
    }

    public static string? FindUf2Volume(string label)
    {
        foreach (var d in DriveInfo.GetDrives())
        {
            try
            {
                if (d.IsReady && d.DriveType == DriveType.Removable &&
                    string.Equals(d.VolumeLabel, label, StringComparison.OrdinalIgnoreCase))
                    return d.RootDirectory.FullName;
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        return null;
    }

    // ---------------- shared: the 1200-baud bootloader touch ----------------
    public static void Touch1200(string port)
    {
        try
        {
            using var sp = new SerialPort(port, 1200) { DtrEnable = true, RtsEnable = true };
            sp.Open();
            Thread.Sleep(150);
            sp.DtrEnable = false;
            sp.Close();
        }
        catch (Exception) { }   // the board resetting mid-close is the point
    }
}
