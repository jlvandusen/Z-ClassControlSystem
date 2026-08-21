// ============================================================
//  LibUsb0 — minimal libusb-win32 (libusb0.dll, 0.1 API) binding.
//  Used for PS3 / Nav pads bound to the libusb-win32 driver that
//  SixaxisPairTool installs: Windows' HID class driver rejects the
//  vendor feature reports (0xF2 / 0xF5) with ERROR_INVALID_PARAMETER,
//  so raw control transfers are the only way on Windows.
//
//  Needs the 32-bit libusb0.dll (SysWOW64) -> bb8 is published win-x86.
// ============================================================

using System.Runtime.InteropServices;

static class LibUsb0
{
    const int PATH_MAX = 512;
    static bool _inited;

    public static bool Available
    {
        get
        {
            try { Init(); return true; }
            catch (DllNotFoundException) { return false; }
            catch (EntryPointNotFoundException) { return false; }
            catch (BadImageFormatException) { return false; }   // 64-bit process, 32-bit dll
        }
    }

    static void Init()
    {
        if (_inited) return;
        usb_init();
        _inited = true;
    }

    public record Dev(IntPtr Ptr, ushort Vid, ushort Pid);

    // Walk bus -> device linked lists reading vid/pid straight out of the
    // packed usb_device_descriptor embedded in struct usb_device.
    public static List<Dev> Find(ushort vid, Func<ushort, bool> pidOk)
    {
        var list = new List<Dev>();
        if (!Available) return list;
        usb_find_busses();
        usb_find_devices();
        int P = IntPtr.Size;
        int busDevices = 2 * P + PATH_MAX;          // struct usb_bus.devices
        int devDescriptor = 3 * P + PATH_MAX;       // struct usb_device.descriptor (packed, 18 bytes)
        for (var bus = usb_get_busses(); bus != IntPtr.Zero; bus = Marshal.ReadIntPtr(bus, 0))
        {
            for (var dev = Marshal.ReadIntPtr(bus, busDevices); dev != IntPtr.Zero; dev = Marshal.ReadIntPtr(dev, 0))
            {
                ushort v = (ushort)Marshal.ReadInt16(dev, devDescriptor + 8);
                ushort p = (ushort)Marshal.ReadInt16(dev, devDescriptor + 10);
                if (v == vid && pidOk(p)) list.Add(new Dev(dev, v, p));
            }
        }
        return list;
    }

    // HID class requests over EP0: GET_REPORT / SET_REPORT, feature type (3)
    const int REQ_IN = 0xA1, REQ_OUT = 0x21, GET_REPORT = 0x01, SET_REPORT = 0x09;

    public static byte[]? GetFeature(Dev d, byte reportId, int len, out int err)
    {
        err = 0;
        var h = usb_open(d.Ptr);
        if (h == IntPtr.Zero) { err = -1; return null; }
        try
        {
            usb_set_configuration(h, 1);
            usb_claim_interface(h, 0);
            var buf = new byte[len];
            int r = usb_control_msg(h, REQ_IN, GET_REPORT, 0x0300 | reportId, 0, buf, len, 5000);
            if (r < 0) { err = r; return null; }
            return buf;
        }
        finally { usb_release_interface(h, 0); usb_close(h); }
    }

    public static bool SetFeature(Dev d, byte reportId, byte[] payload, out int err)
    {
        err = 0;
        var h = usb_open(d.Ptr);
        if (h == IntPtr.Zero) { err = -1; return false; }
        try
        {
            usb_set_configuration(h, 1);
            usb_claim_interface(h, 0);
            int r = usb_control_msg(h, REQ_OUT, SET_REPORT, 0x0300 | reportId, 0, payload, payload.Length, 5000);
            if (r < 0) { err = r; return false; }
            return true;
        }
        finally { usb_release_interface(h, 0); usb_close(h); }
    }

    const string DLL = "libusb0.dll";
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern void usb_init();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern int usb_find_busses();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern int usb_find_devices();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern IntPtr usb_get_busses();
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern IntPtr usb_open(IntPtr dev);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern int usb_close(IntPtr h);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern int usb_set_configuration(IntPtr h, int cfg);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern int usb_claim_interface(IntPtr h, int iface);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)] static extern int usb_release_interface(IntPtr h, int iface);
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    static extern int usb_control_msg(IntPtr h, int requesttype, int request, int value, int index, byte[] bytes, int size, int timeout);
}
