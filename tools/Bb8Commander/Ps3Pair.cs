// ============================================================
//  Ps3Pair — write the ESP32's Bluetooth address into PS3 Sixaxis /
//  DualShock 3 / PS Move Navigation controllers over USB, so they pair
//  to the droid. Same HID feature report (0xF5) that SixaxisPairTool
//  and Linux `sixpair` use — implemented directly on the Windows HID
//  API, so no libusb driver install is needed.
//
//  Report 0xF5 layout (feature), as in Bluepad32 tools/sixaxispairer:
//    [F5] [00] [MAC0..MAC5]   (MAC at offset 2, 8 bytes)
// ============================================================

using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

static class Ps3Pair
{
    public const ushort SONY_VID = 0x054C;
    public static readonly Dictionary<ushort, string> KNOWN_PIDS = new()
    {
        [0x0268] = "Sixaxis / DualShock 3",
        [0x042F] = "PS Move Navigation",
    };

    public record Pad(string Path, ushort Pid, string Name, ushort FeatureLen);

    public static List<Pad> FindPads()
    {
        var list = new List<Pad>();
        HidD_GetHidGuid(out var guid);
        var devs = SetupDiGetClassDevs(ref guid, IntPtr.Zero, IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devs == new IntPtr(-1)) return list;
        try
        {
            var iface = new SP_DEVICE_INTERFACE_DATA { cbSize = Marshal.SizeOf<SP_DEVICE_INTERFACE_DATA>() };
            for (uint i = 0; SetupDiEnumDeviceInterfaces(devs, IntPtr.Zero, ref guid, i, ref iface); i++)
            {
                SetupDiGetDeviceInterfaceDetail(devs, ref iface, IntPtr.Zero, 0, out var need, IntPtr.Zero);
                var buf = Marshal.AllocHGlobal((int)need);
                try
                {
                    Marshal.WriteInt32(buf, IntPtr.Size == 8 ? 8 : 6);   // cbSize of SP_DEVICE_INTERFACE_DETAIL_DATA
                    if (!SetupDiGetDeviceInterfaceDetail(devs, ref iface, buf, need, out _, IntPtr.Zero)) continue;
                    var path = Marshal.PtrToStringUni(buf + 4) ?? "";
                    using var h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
                    if (h.IsInvalid) continue;
                    var attr = new HIDD_ATTRIBUTES { Size = Marshal.SizeOf<HIDD_ATTRIBUTES>() };
                    if (!HidD_GetAttributes(h, ref attr)) continue;
                    if (attr.VendorID != SONY_VID || !KNOWN_PIDS.TryGetValue(attr.ProductID, out var name)) continue;
                    ushort flen = 0;
                    if (HidD_GetPreparsedData(h, out var pp))
                    {
                        if (HidP_GetCaps(pp, out var caps) == HIDP_STATUS_SUCCESS) flen = caps.FeatureReportByteLength;
                        HidD_FreePreparsedData(pp);
                    }
                    list.Add(new Pad(path, attr.ProductID, name, flen));
                }
                finally { Marshal.FreeHGlobal(buf); }
            }
        }
        finally { SetupDiDestroyDeviceInfoList(devs); }
        return list;
    }

    public static byte[]? ReadMaster(Pad pad)
    {
        using var h = Open(pad.Path);
        if (h.IsInvalid) return null;
        var len = Math.Max(pad.FeatureLen, (ushort)9);
        var buf = new byte[len];
        buf[0] = 0xF5;
        if (!HidD_GetFeature(h, buf, buf.Length)) return null;
        return buf.Skip(2).Take(6).ToArray();
    }

    public static bool WriteMaster(Pad pad, byte[] mac)
    {
        using var h = Open(pad.Path);
        if (h.IsInvalid) return false;
        var len = Math.Max(pad.FeatureLen, (ushort)9);
        var buf = new byte[len];
        buf[0] = 0xF5; buf[1] = 0x00;
        Array.Copy(mac, 0, buf, 2, 6);
        return HidD_SetFeature(h, buf, buf.Length);
    }

    public static string Fmt(byte[] m) => string.Join(":", m.Select(b => b.ToString("X2")));

    public static byte[]? ParseMac(string s)
    {
        var parts = s.Trim().Split(':', '-');
        if (parts.Length != 6) return null;
        var mac = new byte[6];
        for (int i = 0; i < 6; i++)
            if (!byte.TryParse(parts[i], System.Globalization.NumberStyles.HexNumber, null, out mac[i])) return null;
        return mac;
    }

    static SafeFileHandle Open(string path) =>
        CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);

    // ---------------- P/Invoke ----------------
    const uint GENERIC_READ = 0x80000000, GENERIC_WRITE = 0x40000000;
    const uint FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2, OPEN_EXISTING = 3;
    const uint DIGCF_PRESENT = 2, DIGCF_DEVICEINTERFACE = 0x10;
    const int HIDP_STATUS_SUCCESS = 0x00110000;

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVICE_INTERFACE_DATA { public int cbSize; public Guid InterfaceClassGuid; public int Flags; public IntPtr Reserved; }

    [StructLayout(LayoutKind.Sequential)]
    struct HIDD_ATTRIBUTES { public int Size; public ushort VendorID; public ushort ProductID; public ushort VersionNumber; }

    [StructLayout(LayoutKind.Sequential)]
    struct HIDP_CAPS
    {
        public ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved;
        public ushort NumberLinkCollectionNodes, NumberInputButtonCaps, NumberInputValueCaps, NumberInputDataIndices,
                      NumberOutputButtonCaps, NumberOutputValueCaps, NumberOutputDataIndices,
                      NumberFeatureButtonCaps, NumberFeatureValueCaps, NumberFeatureDataIndices;
    }

    [DllImport("hid.dll")] static extern void HidD_GetHidGuid(out Guid guid);
    [DllImport("hid.dll")] static extern bool HidD_GetAttributes(SafeFileHandle h, ref HIDD_ATTRIBUTES a);
    [DllImport("hid.dll")] static extern bool HidD_GetFeature(SafeFileHandle h, byte[] buf, int len);
    [DllImport("hid.dll")] static extern bool HidD_SetFeature(SafeFileHandle h, byte[] buf, int len);
    [DllImport("hid.dll")] static extern bool HidD_GetPreparsedData(SafeFileHandle h, out IntPtr pp);
    [DllImport("hid.dll")] static extern bool HidD_FreePreparsedData(IntPtr pp);
    [DllImport("hid.dll")] static extern int HidP_GetCaps(IntPtr pp, out HIDP_CAPS caps);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid g, IntPtr e, IntPtr h, uint flags);
    [DllImport("setupapi.dll")]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr devs, IntPtr devInfo, ref Guid g, uint idx, ref SP_DEVICE_INTERFACE_DATA d);
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr devs, ref SP_DEVICE_INTERFACE_DATA d, IntPtr detail, uint size, out uint need, IntPtr devInfo);
    [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr devs);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
}
