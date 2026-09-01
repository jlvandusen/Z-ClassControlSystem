// Live telemetry charts in the browser: 'bb8 monitor drive --web' serves a
// localhost page and streams every telemetry line to it over SSE. The page
// draws pitch/roll, pot vs target, and PWM in a rolling 60 s window — the
// live version of what 'bb8 analyze' shows after the fact.

using System.Net;
using System.Text;

static class WebMonitor
{
    static HttpListener? _listener;
    static readonly List<StreamWriter> _clients = new();
    static readonly object _lock = new();

    public static bool Active => _listener is not null;

    public static string Start(int port = 8787)
    {
        var url = $"http://127.0.0.1:{port}/";
        _listener = new HttpListener();
        _listener.Prefixes.Add(url);
        _listener.Start();
        var th = new Thread(AcceptLoop) { IsBackground = true };
        th.Start();
        return url;
    }

    static void AcceptLoop()
    {
        while (_listener is { IsListening: true })
        {
            HttpListenerContext ctx;
            try { ctx = _listener.GetContext(); } catch (Exception) { return; }
            try
            {
                if (ctx.Request.Url?.AbsolutePath == "/events")
                {
                    ctx.Response.ContentType = "text/event-stream";
                    ctx.Response.Headers["Cache-Control"] = "no-cache";
                    var w = new StreamWriter(ctx.Response.OutputStream, new UTF8Encoding(false)) { AutoFlush = true };
                    w.Write(": connected\n\n");
                    lock (_lock) _clients.Add(w);
                }
                else
                {
                    var bytes = Encoding.UTF8.GetBytes(PAGE);
                    ctx.Response.ContentType = "text/html; charset=utf-8";
                    ctx.Response.ContentLength64 = bytes.Length;
                    ctx.Response.OutputStream.Write(bytes);
                    ctx.Response.Close();
                }
            }
            catch (Exception) { try { ctx.Response.Abort(); } catch (Exception) { } }
        }
    }

    // Telemetry line "pitch:-1.23,roll:0.45,pot:1502,..." -> SSE JSON
    public static void Push(string board, string line)
    {
        if (_clients.Count == 0) return;
        var sb = new StringBuilder();
        sb.Append("{\"board\":\"").Append(board).Append('"');
        foreach (var part in line.Split(','))
        {
            var i = part.IndexOf(':');
            if (i <= 0) continue;
            var k = part[..i].Trim();
            var v = part[(i + 1)..].Trim();
            if (k.Length == 0 || !double.TryParse(v, System.Globalization.CultureInfo.InvariantCulture, out var d)) continue;
            sb.Append(",\"").Append(k).Append("\":").Append(d.ToString(System.Globalization.CultureInfo.InvariantCulture));
        }
        sb.Append('}');
        var msg = $"data: {sb}\n\n";
        lock (_lock)
        {
            for (int i = _clients.Count - 1; i >= 0; i--)
            {
                try { _clients[i].Write(msg); }
                catch (Exception) { try { _clients[i].Dispose(); } catch (Exception) { } _clients.RemoveAt(i); }
            }
        }
    }

    const string PAGE = """
<!doctype html><html><head><meta charset="utf-8"><title>bb8 live telemetry</title>
<style>
 body{background:#101418;color:#d8dee6;font:13px/1.4 system-ui,sans-serif;margin:14px}
 h1{font-size:15px;margin:0 0 2px} .sub{color:#7b8794;margin-bottom:10px}
 .chart{margin-bottom:14px} canvas{width:100%;height:150px;background:#161b21;border:1px solid #232a33;border-radius:6px}
 .legend{font-size:12px;color:#9aa5b1;margin:2px 0 4px} .legend b{font-weight:600}
</style></head><body>
<h1>bb8 live telemetry</h1><div class="sub" id="st">waiting for telemetry — send 'telemetry on' (or 'telemetry fast') in the monitor</div>
<div class="chart"><div class="legend"><b style="color:#4fc3f7">pitch</b> · <b style="color:#ffb74d">roll</b> (deg)</div><canvas id="c1"></canvas></div>
<div class="chart"><div class="legend"><b style="color:#81c784">pot</b> · <b style="color:#e57373">tgt</b> (counts)</div><canvas id="c2"></canvas></div>
<div class="chart"><div class="legend"><b style="color:#ba68c8">drv</b> · <b style="color:#4db6ac">s2s</b> (PWM)</div><canvas id="c3"></canvas></div>
<script>
const WIN=60000, data=[];
const charts=[{id:'c1',keys:['pitch','roll'],cols:['#4fc3f7','#ffb74d']},
              {id:'c2',keys:['pot','tgt'],cols:['#81c784','#e57373']},
              {id:'c3',keys:['drv','s2s'],cols:['#ba68c8','#4db6ac']}];
let n=0;
const es=new EventSource('/events');
es.onmessage=e=>{const d=JSON.parse(e.data);d._t=Date.now();data.push(d);n++;
  while(data.length&&data[0]._t<Date.now()-WIN)data.shift();
  document.getElementById('st').textContent=`${d.board} — ${n} samples, ${data.length} in window`+(d.hz?` · loop ${d.hz} Hz`:'');};
function draw(){
  const now=Date.now();
  for(const ch of charts){
    const cv=document.getElementById(ch.id);
    if(cv.width!==cv.clientWidth*2){cv.width=cv.clientWidth*2;cv.height=300;}
    const g=cv.getContext('2d');g.clearRect(0,0,cv.width,cv.height);
    let lo=Infinity,hi=-Infinity;
    for(const d of data)for(const k of ch.keys){const v=d[k];if(v!==undefined){if(v<lo)lo=v;if(v>hi)hi=v;}}
    if(lo===Infinity){requestAnimationFrameNext();continue;}
    if(hi-lo<1e-6){hi+=1;lo-=1;}
    const pad=(hi-lo)*0.1;lo-=pad;hi+=pad;
    g.strokeStyle='#232a33';g.beginPath();
    const zy=cv.height-( -lo)/(hi-lo)*cv.height;
    if(lo<0&&hi>0){g.moveTo(0,zy);g.lineTo(cv.width,zy);}g.stroke();
    g.fillStyle='#5a6673';g.font='20px system-ui';
    g.fillText(hi.toFixed(1),6,24);g.fillText(lo.toFixed(1),6,cv.height-8);
    ch.keys.forEach((k,ki)=>{g.strokeStyle=ch.cols[ki];g.lineWidth=2.5;g.beginPath();let started=false;
      for(const d of data){const v=d[k];if(v===undefined)continue;
        const x=(1-(now-d._t)/WIN)*cv.width, y=cv.height-(v-lo)/(hi-lo)*cv.height;
        if(!started){g.moveTo(x,y);started=true;}else g.lineTo(x,y);}
      g.stroke();});
  }
  requestAnimationFrame(draw);
}
function requestAnimationFrameNext(){}
requestAnimationFrame(draw);
</script></body></html>
""";
}
