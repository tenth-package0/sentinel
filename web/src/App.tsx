import { FormEvent, useEffect, useMemo, useRef, useState } from "react";
import {
  Activity,
  AlertTriangle,
  CheckCircle2,
  CircleDot,
  Gauge,
  Play,
  RefreshCcw,
  RotateCcw,
  Send,
  ShieldCheck,
  TerminalSquare,
} from "lucide-react";
import { EngineResult, Position, SentinelClient, TradeInput } from "./engine";

type FeedItem = TradeInput & EngineResult;

const samples: TradeInput[] = [
  { eventId: "TRD-10481", accountId: "ALPHA-7", symbol: "AAPL", side: "BUY", quantity: 350, price: 191.42, eventTimeMs: 1_790_331_200_100, sourceSequence: 1 },
  { eventId: "TRD-10482", accountId: "BETA-2", symbol: "MSFT", side: "SELL", quantity: 620, price: 418.16, eventTimeMs: 1_790_331_200_300, sourceSequence: 2 },
  { eventId: "TRD-10483", accountId: "ALPHA-7", symbol: "AAPL", side: "BUY", quantity: 780, price: 192.01, eventTimeMs: 1_790_331_200_600, sourceSequence: 3 },
  { eventId: "TRD-10484", accountId: "DELTA-4", symbol: "GME", side: "BUY", quantity: 85, price: 24.72, eventTimeMs: 1_790_331_200_900, sourceSequence: 4 },
  { eventId: "TRD-10485", accountId: "BETA-2", symbol: "NVDA", side: "BUY", quantity: 980, price: 178.33, eventTimeMs: 1_790_331_199_000, sourceSequence: 5 },
  { eventId: "TRD-10482", accountId: "BETA-2", symbol: "MSFT", side: "SELL", quantity: 620, price: 418.16, eventTimeMs: 1_790_331_200_300, sourceSequence: 2 },
];

const freshTrade = (): TradeInput => ({
  eventId: `TRD-${Math.floor(11000 + Math.random() * 8000)}`,
  accountId: "ALPHA-7",
  symbol: "AAPL",
  side: "BUY",
  quantity: 100,
  price: 191.42,
  eventTimeMs: Date.now(),
  sourceSequence: Date.now() % 100000,
});

const integer = new Intl.NumberFormat("en-US");

export default function App() {
  const [client, setClient] = useState<SentinelClient>();
  const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
  const [feed, setFeed] = useState<FeedItem[]>([]);
  const [positions, setPositions] = useState<Position[]>([]);
  const [trade, setTrade] = useState<TradeInput>(freshTrade);
  const [running, setRunning] = useState(false);
  const [notice, setNotice] = useState("Engine booting");
  const cancelled = useRef(false);

  useEffect(() => {
    SentinelClient.create()
      .then((loaded) => {
        setClient(loaded);
        setStatus("ready");
        setNotice("C++ engine ready");
      })
      .catch(() => {
        setStatus("error");
        setNotice("Engine failed to load");
      });
    return () => { cancelled.current = true; };
  }, []);

  const alertCount = useMemo(
    () => feed.reduce((count, item) => count + item.alerts.length, 0),
    [feed],
  );
  const criticalCount = useMemo(
    () => feed.flatMap((item) => item.alerts).filter((alert) => alert.severity === "CRITICAL").length,
    [feed],
  );
  const p99 = useMemo(() => {
    if (!feed.length) return 0;
    const timings = feed.map((item) => item.processingTimeNs).sort((a, b) => a - b);
    return timings[Math.max(0, Math.ceil(timings.length * 0.99) - 1)];
  }, [feed]);

  const process = (input: TradeInput) => {
    if (!client) return;
    const result = client.process(input);
    if (result.error) {
      setNotice(result.error);
      return;
    }
    setFeed((current) => [{ ...input, ...result }, ...current]);
    setPositions(client.positions());
    setNotice(result.alerts.length ? `${result.alerts.length} signal${result.alerts.length > 1 ? "s" : ""} generated` : "Trade cleared");
  };

  const submit = (event: FormEvent) => {
    event.preventDefault();
    process({ ...trade, symbol: trade.symbol.toUpperCase(), eventTimeMs: Date.now() });
    setTrade(freshTrade());
  };

  const runDemo = async () => {
    if (!client || running) return;
    cancelled.current = false;
    client.reset();
    setFeed([]);
    setPositions([]);
    setRunning(true);
    setNotice("Replaying market events");
    for (const event of samples) {
      if (cancelled.current) break;
      await new Promise((resolve) => setTimeout(resolve, 360));
      process(event);
    }
    setRunning(false);
    setNotice("Replay complete");
  };

  const reset = () => {
    cancelled.current = true;
    client?.reset();
    setFeed([]);
    setPositions([]);
    setRunning(false);
    setNotice("State cleared");
  };

  const replay = () => {
    if (!client || feed.length === 0) return;
    const result = client.replay();
    setPositions(client.positions());
    setNotice(`${result.replayed} events deterministically replayed`);
  };

  return (
    <div className="app-shell">
      <header className="topbar">
        <a className="brand" href="#top" aria-label="Sentinel home">
          <span className="brand-mark"><ShieldCheck size={19} /></span>
          <span>SENTINEL</span>
          <span className="product-tag">SURVEILLANCE</span>
        </a>
        <div className="engine-status">
          <span className={`status-light ${status}`} />
          <span>{notice}</span>
          <span className="cpp-pill">C++20 · WASM</span>
        </div>
      </header>

      <main id="top">
        <section className="hero">
          <div>
            <p className="eyebrow"><CircleDot size={13} /> REAL-TIME CONTROL PLANE</p>
            <h1>Every trade.<br /><span>Checked before it disappears.</span></h1>
            <p className="hero-copy">A deterministic trade-surveillance engine for position limits, restricted securities, duplicate delivery, and event-order anomalies.</p>
          </div>
          <div className="hero-actions">
            <button className="primary" onClick={runDemo} disabled={status !== "ready" || running}>
              <Play size={16} fill="currentColor" /> {running ? "Replay running" : "Run market replay"}
            </button>
            <button className="secondary" onClick={reset}><RotateCcw size={16} /> Reset state</button>
          </div>
        </section>

        <section className="metrics" aria-label="Engine metrics">
          <Metric icon={<Activity />} label="Processed events" value={integer.format(feed.length)} detail="Idempotent ingestion" />
          <Metric icon={<AlertTriangle />} label="Signals raised" value={integer.format(alertCount)} detail={`${criticalCount} critical`} tone={criticalCount ? "alert" : undefined} />
          <Metric icon={<Gauge />} label="Observed p99" value={feed.length ? (p99 ? `${integer.format(p99)} ns` : "<1 μs") : "—"} detail="In-browser core time" />
          <Metric icon={<CheckCircle2 />} label="Open positions" value={integer.format(positions.length)} detail="Account × symbol" />
        </section>

        <section className="workspace">
          <div className="panel stream-panel">
            <div className="panel-heading">
              <div><span className="section-index">01</span><h2>Decision stream</h2></div>
              <span className="live-label"><span /> LIVE</span>
            </div>
            <div className="table-wrap">
              <table>
                <thead><tr><th>Event</th><th>Account</th><th>Instrument</th><th>Side</th><th>Quantity</th><th>Position</th><th>Decision</th><th>Core time</th></tr></thead>
                <tbody>
                  {feed.length === 0 ? (
                    <tr className="empty-row"><td colSpan={8}><TerminalSquare size={24} /><strong>No events processed</strong><span>Run the replay or submit a trade to start the engine.</span></td></tr>
                  ) : feed.map((item, index) => (
                    <tr key={`${item.eventId}-${index}`}>
                      <td className="mono">{item.eventId}</td><td>{item.accountId}</td><td className="symbol">{item.symbol}</td>
                      <td><span className={`side ${item.side.toLowerCase()}`}>{item.side}</span></td>
                      <td className="mono">{integer.format(item.quantity)}</td><td className="mono">{integer.format(item.positionAfter)}</td>
                      <td>{item.alerts.length ? <span className={`decision ${item.alerts.some((a) => a.severity === "CRITICAL") ? "critical" : "warning"}`}>{item.alerts[0].code.replaceAll("_", " ")}{item.alerts.length > 1 && ` +${item.alerts.length - 1}`}</span> : <span className="decision clear">CLEARED</span>}</td>
                      <td className="mono muted">{integer.format(item.processingTimeNs)} ns</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>

          <aside className="right-rail">
            <div className="panel order-panel">
              <div className="panel-heading compact"><div><span className="section-index">02</span><h2>Inject trade</h2></div></div>
              <form onSubmit={submit}>
                <label>Account<input value={trade.accountId} onChange={(e) => setTrade({ ...trade, accountId: e.target.value })} required /></label>
                <div className="field-row"><label>Symbol<input value={trade.symbol} onChange={(e) => setTrade({ ...trade, symbol: e.target.value })} required /></label><label>Side<select value={trade.side} onChange={(e) => setTrade({ ...trade, side: e.target.value as "BUY" | "SELL" })}><option>BUY</option><option>SELL</option></select></label></div>
                <div className="field-row"><label>Quantity<input type="number" min="1" value={trade.quantity} onChange={(e) => setTrade({ ...trade, quantity: Number(e.target.value) })} required /></label><label>Price<input type="number" min="0.01" step="0.01" value={trade.price} onChange={(e) => setTrade({ ...trade, price: Number(e.target.value) })} required /></label></div>
                <button className="primary full" disabled={status !== "ready"}><Send size={15} /> Send to engine</button>
              </form>
            </div>

            <div className="panel rules-panel">
              <div className="panel-heading compact"><div><span className="section-index">03</span><h2>Active controls</h2></div></div>
              <Rule code="POS-01" title="Position limit" value="±1,000 shares" />
              <Rule code="RES-02" title="Restricted symbols" value="GME · LOCK" />
              <Rule code="NOT-03" title="Large notional" value="> $250,000" />
              <Rule code="SEQ-04" title="Event watermark" value="Per account" />
            </div>
          </aside>
        </section>

        <section className="lower-grid">
          <div className="panel positions-panel">
            <div className="panel-heading"><div><span className="section-index">04</span><h2>Position ledger</h2></div><button className="text-button" onClick={replay} disabled={!feed.length}><RefreshCcw size={14} /> Replay events</button></div>
            {positions.length === 0 ? <p className="empty-copy">Positions will appear after the first accepted event.</p> : <div className="position-grid">{positions.map((position) => <div className="position" key={`${position.accountId}-${position.symbol}`}><span>{position.accountId}</span><strong>{position.symbol}</strong><b className={position.quantity < 0 ? "negative" : "positive"}>{position.quantity > 0 ? "+" : ""}{integer.format(position.quantity)}</b></div>)}</div>}
          </div>
          <div className="architecture">
            <p className="eyebrow">ENGINE ARCHITECTURE</p>
            <h2>One core. Reproducible decisions.</h2>
            <p>Trade events pass through validation, deduplication, state projection, independent controls, and an append-only audit result. The C++ core runs here through WebAssembly.</p>
            <div className="pipeline"><span>EVENT</span><i>→</i><span>VALIDATE</span><i>→</i><span>RULES</span><i>→</i><span>COMMIT</span><i>→</i><span>AUDIT</span></div>
          </div>
        </section>
      </main>
      <footer><span>SENTINEL / ENGINE v0.2</span><span>Built by Kebron Tadesse</span><span>Deterministic · Auditable · Extensible</span></footer>
    </div>
  );
}

function Metric({ icon, label, value, detail, tone }: { icon: React.ReactNode; label: string; value: string; detail: string; tone?: string }) {
  return <article className={`metric ${tone ?? ""}`}><div className="metric-icon">{icon}</div><div><span>{label}</span><strong>{value}</strong><small>{detail}</small></div></article>;
}

function Rule({ code, title, value }: { code: string; title: string; value: string }) {
  return <div className="rule"><span>{code}</span><div><strong>{title}</strong><small>{value}</small></div><i /> </div>;
}
