// Updating from GitHub Releases over plain HTTPS - the no-git path (BASIC
// installs). The tag is discovered from the /releases/latest redirect (no API
// quota, no auth), the zip comes from /releases/latest-style download URLs,
// and it is extracted over the install folder. bb8.exe itself can't be
// overwritten while running, so it lands as bb8.exe.new and the bb8.cmd
// wrapper swaps it in on the next invocation.

using System.IO.Compression;

record ApplyResult(int Files, bool Bb8Updated, bool FirmwareChanged);

static class ReleaseUpdate
{
    // "v1.02" from the Location header of {repo}/releases/latest (a 302).
    public static async Task<string?> LatestTag(string repo)
    {
        try
        {
            using var handler = new HttpClientHandler { AllowAutoRedirect = false };
            using var http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(10) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("bb8-commander");
            using var resp = await http.GetAsync($"{repo}/releases/latest");
            var loc = resp.Headers.Location?.ToString();
            if (loc is null) return null;
            var tag = loc.TrimEnd('/').Split('/')[^1];
            return string.IsNullOrWhiteSpace(tag) || tag is "releases" or "latest" ? null : tag;
        }
        catch (Exception) { return null; }
    }

    public static Version? ParseVersion(string s)
    {
        var m = System.Text.RegularExpressions.Regex.Match(s, @"v?(\d+(?:\.\d+)+)");
        return m.Success && Version.TryParse(m.Groups[1].Value, out var v) ? v : null;
    }

    // The VERSION file every release writes at the install root. Returns the
    // literal "1.02"-style text — .NET's Version would print 1.01 as "1.1".
    public static string? LocalVersionText(string root)
    {
        var p = Path.Combine(root, "VERSION");
        if (!File.Exists(p)) return null;
        try
        {
            var m = System.Text.RegularExpressions.Regex.Match(
                File.ReadLines(p).FirstOrDefault() ?? "", @"v(\d+(?:\.\d+)+)");
            return m.Success ? m.Groups[1].Value : null;
        }
        catch (Exception) { return null; }
    }

    static async Task<string?> FetchString(HttpClient http, string url)
    {
        try
        {
            using var resp = await http.GetAsync(url);
            return resp.IsSuccessStatusCode ? await resp.Content.ReadAsStringAsync() : null;
        }
        catch (Exception) { return null; }
    }

    public static async Task<ApplyResult?> Apply(string repo, string tag, string root, string exeDir)
    {
        var asset = $"ZClass-ControlSystem-{tag}.zip";
        var url = $"{repo}/releases/download/{tag}/{asset}";
        var tmp = Path.Combine(Path.GetTempPath(), $"bb8-{asset}");
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("bb8-commander");
            using (var resp = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead))
            {
                if (!resp.IsSuccessStatusCode)
                {
                    Console.WriteLine($"[UPDATE] HTTP {(int)resp.StatusCode} for {asset}");
                    return null;
                }
                long len = resp.Content.Headers.ContentLength ?? -1;
                await using var body = await resp.Content.ReadAsStreamAsync();
                await using var fs = File.Create(tmp);
                var buf = new byte[81920];
                long done = 0; int n, lastPct = -1;
                while ((n = await body.ReadAsync(buf)) > 0)
                {
                    await fs.WriteAsync(buf.AsMemory(0, n));
                    done += n;
                    int pct = len > 0 ? (int)(done * 100 / len) : -1;
                    if (pct >= 0 && pct / 10 != lastPct) { Console.Write($"\r[UPDATE] downloading {pct}% ({done / 1048576} MB)"); lastPct = pct / 10; }
                }
                Console.WriteLine($"\r[UPDATE] downloaded {done / 1048576} MB              ");
            }

            // Integrity: releases ship a SHA256SUMS asset — verify when present.
            var sums = await FetchString(http, $"{repo}/releases/download/{tag}/SHA256SUMS-{tag}.txt");
            if (sums is not null)
            {
                var expected = sums.Split('\n')
                    .Select(l => l.Trim())
                    .FirstOrDefault(l => l.EndsWith(asset, StringComparison.OrdinalIgnoreCase))?
                    .Split(' ')[0];
                if (expected is not null)
                {
                    using var sha = System.Security.Cryptography.SHA256.Create();
                    await using var check = File.OpenRead(tmp);
                    var actual = Convert.ToHexString(await sha.ComputeHashAsync(check)).ToLowerInvariant();
                    if (!actual.Equals(expected, StringComparison.OrdinalIgnoreCase))
                    {
                        Console.WriteLine("[UPDATE] SHA256 MISMATCH — download corrupt or tampered, not applying.");
                        return null;
                    }
                    Console.WriteLine("[UPDATE] SHA256 verified.");
                }
            }

            int files = 0; bool bb8New = false, fw = false;
            using (var zip = ZipFile.OpenRead(tmp))
            {
                foreach (var e in zip.Entries)
                {
                    if (e.Name.Length == 0) continue;                     // directory entry
                    var rel = e.FullName.Replace('\\', '/');
                    var cut = rel.IndexOf('/');
                    if (cut < 0) continue;                                // no top-level folder = not ours
                    rel = rel[(cut + 1)..];
                    if (rel.Length == 0 || rel.Contains("..")) continue;  // traversal guard

                    string dest;
                    if (rel.StartsWith("bb8/", StringComparison.OrdinalIgnoreCase))
                    {
                        var name = rel[4..];
                        if (name.Contains('/')) continue;
                        if (name.Equals("bb8.exe", StringComparison.OrdinalIgnoreCase)) { name = "bb8.exe.new"; bb8New = true; }
                        dest = Path.Combine(exeDir, name);
                    }
                    else
                    {
                        if (rel.StartsWith("binaries/", StringComparison.OrdinalIgnoreCase) ||
                            rel.StartsWith("firmware/", StringComparison.OrdinalIgnoreCase)) fw = true;
                        dest = Path.Combine(root, rel.Replace('/', Path.DirectorySeparatorChar));
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                    e.ExtractToFile(dest, overwrite: true);
                    files++;
                }
            }
            return new ApplyResult(files, bb8New, fw);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[UPDATE] {ex.Message}");
            return null;
        }
        finally { try { File.Delete(tmp); } catch (Exception) { } }
    }
}
