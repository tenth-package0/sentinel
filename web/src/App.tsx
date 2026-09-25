import { FormEvent, ReactNode, useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Activity,
  AlertTriangle,
  CheckCircle2,
  CircleDot,
  Download,
  Gauge,
  Pause,
  Play,
  Radio,
  RefreshCcw,
  RotateCcw,
  Search,
  Send,
  ShieldCheck,
  SlidersHorizontal,
  TerminalSquare,
  X,
  Zap,
} from "lucide-react";
import { Alert, EngineResult, Position, SentinelClient, TradeInput } from "./engine";
import { COLORS, Donut, FlowChart, Slice, Sparkline, trailingRate, useCountUp, useFlowBuckets } from "./charts";
import { MarketSim, Quote } from "./sim";

type Outcome = "clear" | "review" | "critical" | "duplicate";
type FeedItem = TradeInput & EngineResult & { seq: number; notional: number; outcome: Outcome };
type Totals = { processed: number; committed: number; duplicates: number; warnings: number; critical: number; byCode: Record<string, number> };
type Mode = "idle" | "scenario" | "live" | "stress";
type Filter = "all" | "signals" | "critical" | "clear";
type StressResult = { events: number; rate: number; nsPerEvent: number };
type Particle = { id: number; outcome: Outcome };

const samples: TradeInput[] = [
  { eventId: "TRD-10481", accountId: "ALPHA-7", symbol: "AAPL", side: "BUY", quantity: 350, price: 191.42, eventTimeMs: 1_790_331_200_100 },
  { eventId: "TRD-10482", accountId: "BETA-2", symbol: "MSFT", side: "SELL", quantity: 620, price: 418.16, eventTimeMs: 1_790_331_200_300 },
  { eventId: "TRD-10483", accountId: "ALPHA-7", symbol: "AAPL", side: "BUY", quantity: 780, price: 192.01, eventTimeMs: 1_790_331_200_600 },
  { eventId: "TRD-10484", accountId: "DELTA-4", symbol: "GME", side: "BUY", quantity: 85, price: 24.72, eventTimeMs: 1_790_331_200_900 },
  { eventId: "TRD-10485", accountId: "BETA-2", symbol: "NVDA", side: "BUY", quantity: 980, price: 178.33, eventTimeMs: 1_790_331_199_000 },
  { eventId: "TRD-10482", accountId: "BETA-2", symbol: "MSFT", side: "SELL", quantity: 620, price: 418.16, eventTimeMs: 1_790_331_200_300 },
];

const RULE_META: Record<string, { label: string; color: string; code: string }> = {
  POSITION_LIMIT_BREACH: { label: "Position limit", color: COLORS.red, code: "POS-01" },
  RESTRICTED_SYMBOL: { label: "Restricted symbol", color: "#ff9bd0", code: "RES-02" },
  LARGE_NOTIONAL: { label: "Large notional", color: COLORS.amber, code: "NOT-03" },
  OUT_OF_ORDER_EVENT: { label: "Out of order", color: COLORS.blue, code: "SEQ-04" },
  DUPLICATE_EVENT: { label: "Duplicate", color: "#8a9d94", code: "IDM-05" },
};

const FEED_CAP = 400;
const STRESS_EVENTS = 1_000_000;
const RESTRICTED = ["GME", "LOCK", "RESTRICTED"];

const freshTrade = (): TradeInput => ({
  eventId: `TRD-${Math.floor(11000 + Math.random() * 8000)}`,
  accountId: "ALPHA-7",
  symbol: "AAPL",
  side: "BUY",
  quantity: 100,
  price: 191.42,
  eventTimeMs: Date.now(),
});

const emptyTotals = (): Totals => ({ processed: 0, committed: 0, duplicates: 0, warnings: 0, critical: 0, byCode: {} });
const integer = new Intl.NumberFormat("en-US");
const money = new Intl.NumberFormat("en-US", { style: "currency", currency: "USD", maximumFractionDigits: 0 });
const compact = new Intl.NumberFormat("en-US", { notation: "compact", maximumFractionDigits: 2 });
const posKey = (account: string, symbol: string) => `${account}|${symbol}`;

function classify(result: EngineResult): Outcome {
  if (result.duplicate) return "duplicate";
  if (result.alerts.some((a) => a.severity === "CRITICAL")) return "critical";
  if (result.alerts.some((a) => a.severity === "WARNING")) return "review";
  return "clear";
}

export default function App() {
  const [client, setClient] = useState<SentinelClient>();
  const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
  const [notice, setNotice] = useState("Engine booting");
  const [feed, setFeed] = useState<FeedItem[]>([]);
  const [totals, setTotals] = useState<Totals>(emptyTotals);
  const [positions, setPositions] = useState<Position[]>([]);
  const [trade, setTrade] = useState<TradeInput>(freshTrade);
  const [mode, setMode] = useState<Mode>("idle");
  const [speed, setSpeed] = useState(1);
  const [limits, setLimits] = useState({ position: 1_000, notional: 250_000 });
  const [draft, setDraft] = useState({ position: "1000", notional: "250000" });
  const [filter, setFilter] = useState<Filter>("all");
  const [query, setQuery] = useState("");
  const [selected, setSelected] = useState<FeedItem>();
  const [stress, setStress] = useState<StressResult>();
  const [quotes, setQuotes] = useState<Quote[]>([]);
  const [rateSeries, setRateSeries] = useState<number[]>([]);
  const [particles, setParticles] = useState<Particle[]>([]);

  const flow = useFlowBuckets();
  const sim = useRef(new MarketSim());
  const history = useRef<TradeInput[]>([]);
  const positionMap = useRef(new Map<string, number>());
  const positionHistory = useRef(new Map<string, number[]>());
  const seq = useRef(0);
  const particleId = useRef(0);
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
    setQuotes(sim.current.quotes());
    const ticker = setInterval(() => {
      sim.current.tick();
      setQuotes(sim.current.quotes());
    }, 1_200);
    const sampler = setInterval(() => {
      setRateSeries((s) => [...s.slice(-39), trailingRate(flow.ref.current, 3)]);
    }, 500);
    return () => {
      cancelled.current = true;
      clearInterval(ticker);
      clearInterval(sampler);
    };
  }, [flow.ref]);

  const syncPositions = useCallback((engine: SentinelClient) => {
    const next = engine.positions();
    const map = new Map<string, number>();
    for (const p of next) {
      const key = posKey(p.accountId, p.symbol);
      map.set(key, p.quantity);
      const series = positionHistory.current.get(key) ?? [0];
      if (series[series.length - 1] !== p.quantity) {
        series.push(p.quantity);
        if (series.length > 32) series.shift();
      }
      positionHistory.current.set(key, series);
    }
    positionMap.current = map;
    setPositions(next);
  }, []);

  /**
   * Sends a batch through the C++ engine and folds results into UI state with a
   * single render. `animate` drives the flow chart and pipeline particles.
   */
  const processBatch = useCallback((engine: SentinelClient, inputs: TradeInput[], animate = true) => {
    const items: FeedItem[] = [];
    const delta = emptyTotals();
    for (const input of inputs) {
      const result = engine.process(input);
      if (result.error) {
        setNotice(result.error);
        continue;
      }
      history.current.push(input);
      const outcome = classify(result);
      const warnings = result.alerts.filter((a) => a.severity === "WARNING").length;
      const critical = result.alerts.filter((a) => a.severity === "CRITICAL").length;
      delta.processed++;
      if (result.duplicate) delta.duplicates++;
      else delta.committed++;
      delta.warnings += warnings;
      delta.critical += critical;
      for (const alert of result.alerts) delta.byCode[alert.code] = (delta.byCode[alert.code] ?? 0) + 1;
      if (animate) flow.record(warnings, critical);
      items.push({ ...input, ...result, seq: ++seq.current, notional: input.quantity * input.price, outcome });
    }
    if (!items.length) return items;
    const newestFirst = [...items].reverse();
    setFeed((current) => [...newestFirst, ...current].slice(0, FEED_CAP));
    setTotals((t) => {
      const byCode = { ...t.byCode };
      for (const [code, n] of Object.entries(delta.byCode)) byCode[code] = (byCode[code] ?? 0) + n;
      return {
        processed: t.processed + delta.processed,
        committed: t.committed + delta.committed,
        duplicates: t.duplicates + delta.duplicates,
        warnings: t.warnings + delta.warnings,
        critical: t.critical + delta.critical,
        byCode,
      };
    });
    if (animate) {
      const spawned = items.slice(0, 3).map((item) => ({ id: ++particleId.current, outcome: item.outcome }));
      setParticles((p) => [...p, ...spawned].slice(-16));
    }
    syncPositions(engine);
    return items;
  }, [flow, syncPositions]);

  const clearView = () => {
    history.current = [];
    positionMap.current = new Map();
    positionHistory.current = new Map();
    flow.ref.current = [];
    setFeed([]);
    setTotals(emptyTotals());
    setPositions([]);
    setSelected(undefined);
  };

  /** Resets the engine and deterministically re-ingests the retained history. */
  const rebuild = (engine: SentinelClient, events: TradeInput[]) => {
    engine.reset();
    const keep = positionHistory.current;
    clearView();
    positionHistory.current = keep;
    return processBatch(engine, events, false);
  };

  // Live market flow: a steady stream of generated trades through the engine.
  useEffect(() => {
    if (mode !== "live" || !client) return;
    const perSecond = 10 * speed;
    const interval = 100;
    let carry = 0;
    const timer = setInterval(() => {
      carry += (perSecond * interval) / 1000;
      const count = Math.floor(carry);
      carry -= count;
      if (!count) return;
      const batch = Array.from({ length: count }, () =>
        sim.current.next((a, s) => positionMap.current.get(posKey(a, s)) ?? 0, limits.position),
      );
      const items = processBatch(client, batch);
      const flagged = items.find((i) => i.outcome === "critical");
      setNotice(flagged ? `${flagged.alerts[0].code.replaceAll("_", " ")} · ${flagged.accountId} ${flagged.symbol}` : "Streaming live order flow");
    }, interval);
    return () => clearInterval(timer);
  }, [mode, client, speed, limits.position, processBatch]);

  const process = (input: TradeInput) => {
    if (!client) return;
    const [item] = processBatch(client, [input]);
    if (item) setNotice(item.alerts.length ? `${item.alerts.length} signal${item.alerts.length > 1 ? "s" : ""} generated` : "Trade cleared");
  };

  const submit = (event: FormEvent) => {
    event.preventDefault();
    process({ ...trade, symbol: trade.symbol.toUpperCase(), eventTimeMs: Date.now() });
    setTrade(freshTrade());
  };

  const runScenario = async () => {
    if (!client || mode !== "idle") return;
    cancelled.current = false;
    client.reset();
    clearView();
    setMode("scenario");
    setNotice("Replaying market scenario");
    for (const event of samples) {
      await new Promise((resolve) => setTimeout(resolve, 520 / speed));
      if (cancelled.current) break;
      process(event);
    }
    setMode("idle");
    if (!cancelled.current) setNotice("Scenario complete");
  };

  const toggleLive = () => {
    if (!client) return;
    if (mode === "live") {
      setMode("idle");
      setNotice("Live feed paused");
    } else if (mode === "idle") {
      setMode("live");
      setNotice("Streaming live order flow");
    }
  };

  const runStress = async () => {
    if (!client || mode !== "idle") return;
    setMode("stress");
    setStress(undefined);
    setNotice(`Running ${integer.format(STRESS_EVENTS)} trades through the engine`);
    // Best of three runs: the fastest run is the one least disturbed by the OS
    // and by the browser still optimising the WebAssembly.
    const runs: number[] = [];
    for (let i = 0; i < 3; i++) {
      await new Promise((resolve) => setTimeout(resolve, 30));
      runs.push(client.benchmark(STRESS_EVENTS));
    }
    const nsPerEvent = Math.min(...runs);
    setStress({ events: STRESS_EVENTS, rate: 1e9 / nsPerEvent, nsPerEvent });
    setMode("idle");
    setNotice("Stress test complete · dashboard state untouched");
  };

  const reset = () => {
    cancelled.current = true;
    client?.reset();
    clearView();
    setMode("idle");
    setNotice("State cleared");
  };

  const replay = () => {
    if (!client || !history.current.length) return;
    const result = client.replay();
    syncPositions(client);
    setNotice(`${integer.format(result.replayed)} events deterministically replayed`);
  };

  const applyLimits = () => {
    if (!client || mode !== "idle") return;
    const position = Math.max(1, Math.floor(Number(draft.position)));
    const notional = Math.max(1, Number(draft.notional));
    if (!Number.isFinite(position) || !Number.isFinite(notional)) return;
    const retained = [...history.current];
    client.configure(position, notional);
    setLimits({ position, notional });
    const items = rebuild(client, retained);
    const flagged = items.filter((i) => i.alerts.length).length;
    setNotice(`Controls updated · ${integer.format(retained.length)} events re-evaluated, ${flagged} flagged`);
  };

  const exportLog = (format: "json" | "csv") => {
    const rows = [...feed].reverse();
    let body: string;
    if (format === "json") {
      body = JSON.stringify({ exportedAt: new Date().toISOString(), limits, events: rows.map(({ seq: _s, ...r }) => r) }, null, 2);
    } else {
      const header = ["eventId", "accountId", "symbol", "side", "quantity", "price", "notional", "eventTimeMs", "duplicate", "positionAfter", "outcome", "alerts"];
      const escape = (v: unknown) => `"${String(v).replaceAll('"', '""')}"`;
      body = [header.join(","), ...rows.map((r) => [r.eventId, r.accountId, r.symbol, r.side, r.quantity, r.price, r.notional.toFixed(2), r.eventTimeMs, r.duplicate, r.positionAfter, r.outcome, r.alerts.map((a) => a.code).join("|")].map(escape).join(","))].join("\n");
    }
    const url = URL.createObjectURL(new Blob([body], { type: format === "json" ? "application/json" : "text/csv" }));
    const link = document.createElement("a");
    link.href = url;
    link.download = `sentinel-decisions-${Date.now()}.${format}`;
    link.click();
    URL.revokeObjectURL(url);
  };

  const visible = useMemo(() => {
    const q = query.trim().toUpperCase();
    return feed.filter((item) => {
      if (filter === "signals" && !item.alerts.length) return false;
      if (filter === "critical" && item.outcome !== "critical") return false;
      if (filter === "clear" && item.outcome !== "clear") return false;
      if (!q) return true;
      return item.eventId.includes(q) || item.accountId.includes(q) || item.symbol.includes(q);
    }).slice(0, 120);
  }, [feed, filter, query]);

  const exposures = useMemo(() => positions
    .map((p) => ({ ...p, key: posKey(p.accountId, p.symbol), utilization: Math.abs(p.quantity) / limits.position }))
    .sort((a, b) => b.utilization - a.utilization), [positions, limits.position]);
  const breaches = exposures.filter((e) => e.utilization > 1).length;

  const slices: Slice[] = Object.entries(RULE_META).filter(([key]) => key !== "DUPLICATE_EVENT").map(([key, meta]) => ({ key, label: meta.label, color: meta.color, value: totals.byCode[key] ?? 0 }));
  const signalTotal = slices.reduce((s, x) => s + x.value, 0);
  const liveRate = rateSeries[rateSeries.length - 1] ?? 0;
  const busy = mode !== "idle";

  return (
    <div className="app-shell">
      <header className="topbar">
        <a className="brand" href="#top" aria-label="Sentinel home">
          <span className="brand-mark"><ShieldCheck size={19} /></span>
          <span>SENTINEL</span>
          <span className="product-tag">SURVEILLANCE</span>
        </a>
        <div className="engine-status" aria-live="polite">
          <span className={`status-light ${status} ${mode === "live" ? "live" : ""}`} />
          <span key={notice} className="notice">{notice}</span>
          <span className="cpp-pill">C++20 · WASM</span>
        </div>
      </header>

      <Ticker quotes={quotes} />

      <main id="top">
        <section className="hero">
          <div>
            <p className="eyebrow"><CircleDot size={13} /> REAL-TIME CONTROL PLANE</p>
            <h1>Every trade.<br /><span>Checked before it disappears.</span></h1>
            <p className="hero-copy">A deterministic trade-surveillance engine for position limits, restricted securities, duplicate delivery, and event-order anomalies — the C++ core running live in your browser.</p>
          </div>
          <div className="hero-controls">
            <div className="hero-actions">
              <button className={`primary ${mode === "live" ? "is-live" : ""}`} onClick={toggleLive} disabled={status !== "ready" || (busy && mode !== "live")}>
                {mode === "live" ? <><Pause size={15} fill="currentColor" /> Pause live feed</> : <><Radio size={15} /> Start live feed</>}
              </button>
              <button className="secondary" onClick={runScenario} disabled={status !== "ready" || busy}>
                <Play size={15} /> {mode === "scenario" ? "Scenario running" : "Run scenario"}
              </button>
              <button className="secondary" onClick={runStress} disabled={status !== "ready" || busy}>
                <Zap size={15} /> Stress test
              </button>
              <button className="secondary icon-only" onClick={reset} aria-label="Reset state" title="Reset state"><RotateCcw size={15} /></button>
            </div>
            <div className="speed" role="group" aria-label="Playback speed">
              <span>SPEED</span>
              {[0.5, 1, 2, 4].map((s) => (
                <button key={s} className={speed === s ? "active" : ""} onClick={() => setSpeed(s)}>{s}×</button>
              ))}
            </div>
          </div>
        </section>

        <section className="metrics" aria-label="Engine metrics">
          <Metric icon={<Activity />} label="Processed events" value={totals.processed} detail={`${integer.format(totals.duplicates)} duplicates absorbed`} spark={rateSeries} />
          <Metric icon={<AlertTriangle />} label="Signals raised" value={totals.warnings + totals.critical} detail={`${integer.format(totals.critical)} critical`} tone={totals.critical ? "alert" : undefined} />
          <Metric
            icon={<Gauge />}
            label={stress ? "WASM throughput" : "Live flow"}
            value={stress ? stress.rate : liveRate}
            format={(v) => (stress ? compact.format(v) : v.toFixed(1))}
            unit="ev/s"
            detail={stress ? `${stress.nsPerEvent.toFixed(1)} ns/trade · in WebAssembly` : "Trailing 3s rate"}
            spark={stress ? undefined : rateSeries}
          />
          <Metric icon={<CheckCircle2 />} label="Open positions" value={positions.length} detail={breaches ? `${breaches} over limit` : "Account × symbol"} tone={breaches ? "alert" : undefined} />
        </section>

        <section className="analytics">
          <div className="panel flow-panel">
            <div className="panel-heading">
              <div><span className="section-index">01</span><h2>Event flow</h2></div>
              <div className="legend">
                <span><i style={{ background: COLORS.green }} /> Events/s</span>
                <span><i style={{ background: COLORS.amber }} /> Warning</span>
                <span><i style={{ background: COLORS.red }} /> Critical</span>
              </div>
            </div>
            <FlowChart buckets={flow.ref} />
            {mode === "stress" && (
              <div className="stress-overlay">
                <Zap size={18} />
                <strong>{integer.format(STRESS_EVENTS)} trades</strong>
                <div className="progress indeterminate"><span /></div>
                <small>Same workload as the native benchmark · best of 3 runs</small>
              </div>
            )}
          </div>
          <div className="panel mix-panel">
            <div className="panel-heading compact"><div><span className="section-index">02</span><h2>Signal mix</h2></div></div>
            <div className="mix-body">
              <Donut slices={slices} total={signalTotal} />
              <ul className="mix-legend">
                {slices.map((s) => (
                  <li key={s.key}>
                    <i style={{ background: s.color }} />
                    <span>{s.label}</span>
                    <b>{integer.format(s.value)}</b>
                    <em><span style={{ width: `${signalTotal ? (s.value / signalTotal) * 100 : 0}%`, background: s.color }} /></em>
                  </li>
                ))}
              </ul>
            </div>
          </div>
        </section>

        <Pipeline particles={particles} totals={totals} onDone={(id) => setParticles((p) => p.filter((x) => x.id !== id))} />

        <section className="workspace">
          <div className="panel stream-panel">
            <div className="panel-heading">
              <div><span className="section-index">03</span><h2>Decision stream</h2></div>
              <div className="stream-tools">
                <label className="search"><Search size={13} /><input placeholder="Event, account, symbol" value={query} onChange={(e) => setQuery(e.target.value)} /></label>
                <div className="chips" role="tablist">
                  {(["all", "signals", "critical", "clear"] as Filter[]).map((f) => (
                    <button key={f} role="tab" aria-selected={filter === f} className={filter === f ? "active" : ""} onClick={() => setFilter(f)}>{f}</button>
                  ))}
                </div>
                <button className="text-button" onClick={() => exportLog("csv")} disabled={!feed.length}><Download size={13} /> CSV</button>
                <button className="text-button" onClick={() => exportLog("json")} disabled={!feed.length}><Download size={13} /> JSON</button>
              </div>
            </div>
            <div className="table-wrap">
              <table>
                <thead><tr><th>Event</th><th>Account</th><th>Instrument</th><th>Side</th><th>Quantity</th><th>Notional</th><th>Position</th><th>Decision</th></tr></thead>
                <tbody>
                  {visible.length === 0 ? (
                    <tr className="empty-row"><td colSpan={8}><TerminalSquare size={24} /><strong>{feed.length ? "No events match" : "No events processed"}</strong><span>{feed.length ? "Adjust the filter or search." : "Start the live feed, run the scenario, or inject a trade."}</span></td></tr>
                  ) : visible.map((item) => (
                    <tr key={item.seq} className={`row-${item.outcome} ${selected?.seq === item.seq ? "selected" : ""}`} onClick={() => setSelected(item)} tabIndex={0} onKeyDown={(e) => e.key === "Enter" && setSelected(item)}>
                      <td className="mono">{item.eventId}</td><td>{item.accountId}</td><td className="symbol">{item.symbol}</td>
                      <td><span className={`side ${item.side.toLowerCase()}`}>{item.side}</span></td>
                      <td className="mono">{integer.format(item.quantity)}</td>
                      <td className="mono muted">{money.format(item.notional)}</td>
                      <td className="mono">{integer.format(item.positionAfter)}</td>
                      <td><Decision item={item} /></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>

          <aside className="right-rail">
            <div className="panel order-panel">
              <div className="panel-heading compact"><div><span className="section-index">04</span><h2>Inject trade</h2></div></div>
              <form onSubmit={submit}>
                <label>Account<input value={trade.accountId} onChange={(e) => setTrade({ ...trade, accountId: e.target.value })} required /></label>
                <div className="field-row"><label>Symbol<input value={trade.symbol} onChange={(e) => setTrade({ ...trade, symbol: e.target.value })} required /></label><label>Side<select value={trade.side} onChange={(e) => setTrade({ ...trade, side: e.target.value as "BUY" | "SELL" })}><option>BUY</option><option>SELL</option></select></label></div>
                <div className="field-row"><label>Quantity<input type="number" min="1" value={trade.quantity} onChange={(e) => setTrade({ ...trade, quantity: Number(e.target.value) })} required /></label><label>Price<input type="number" min="0.01" step="0.01" value={trade.price} onChange={(e) => setTrade({ ...trade, price: Number(e.target.value) })} required /></label></div>
                <p className="notional-preview">Notional <b className={trade.quantity * trade.price > limits.notional ? "over" : ""}>{money.format(trade.quantity * trade.price)}</b></p>
                <button className="primary full" disabled={status !== "ready" || mode === "stress"}><Send size={15} /> Send to engine</button>
              </form>
            </div>

            <div className="panel rules-panel">
              <div className="panel-heading compact"><div><span className="section-index">05</span><h2>Active controls</h2></div><SlidersHorizontal size={14} className="heading-icon" /></div>
              <div className="rule editable">
                <span>POS-01</span>
                <div><strong>Position limit</strong><small>± shares per account × symbol</small></div>
                <input type="number" min="1" value={draft.position} onChange={(e) => setDraft({ ...draft, position: e.target.value })} aria-label="Position limit" />
              </div>
              <div className="rule editable">
                <span>NOT-03</span>
                <div><strong>Large notional</strong><small>USD per trade</small></div>
                <input type="number" min="1" value={draft.notional} onChange={(e) => setDraft({ ...draft, notional: e.target.value })} aria-label="Notional threshold" />
              </div>
              <Rule code="RES-02" title="Restricted symbols" value={RESTRICTED.join(" · ")} />
              <Rule code="SEQ-04" title="Event watermark" value="Per account" />
              <Rule code="IDM-05" title="Idempotency" value="Event ID" />
              <div className="apply-row">
                <small>Applying re-evaluates all retained events under the new controls.</small>
                <button className="secondary" onClick={applyLimits} disabled={!client || busy || (String(limits.position) === draft.position && String(limits.notional) === draft.notional)}>Apply</button>
              </div>
            </div>
          </aside>
        </section>

        <section className="lower-grid">
          <div className="panel positions-panel">
            <div className="panel-heading">
              <div><span className="section-index">06</span><h2>Exposure monitor</h2></div>
              <button className="text-button" onClick={replay} disabled={!history.current.length || busy}><RefreshCcw size={14} /> Replay events</button>
            </div>
            {exposures.length === 0 ? <p className="empty-copy">Positions will appear after the first accepted event.</p> : (
              <div className="exposure-list">
                <div className="exposure-scale"><span>−{integer.format(limits.position)}</span><span>0</span><span>+{integer.format(limits.position)}</span></div>
                {exposures.slice(0, 14).map((e) => {
                  const pct = Math.min(1.5, e.utilization) / 1.5 * 50;
                  return (
                    <div className={`exposure ${e.utilization > 1 ? "breach" : e.utilization > 0.8 ? "near" : ""}`} key={e.key}>
                      <div className="exposure-id"><strong>{e.symbol}</strong><span>{e.accountId}</span></div>
                      <div className="exposure-track">
                        <i className="limit-mark left" /><i className="limit-mark right" /><i className="zero-mark" />
                        <span className={`exposure-bar ${e.quantity < 0 ? "short" : "long"}`} style={{ width: `${pct}%`, [e.quantity < 0 ? "right" : "left"]: "50%" }} />
                      </div>
                      <b className={e.quantity < 0 ? "negative" : "positive"}>{e.quantity > 0 ? "+" : ""}{integer.format(e.quantity)}</b>
                      <Sparkline values={positionHistory.current.get(e.key) ?? []} color={e.utilization > 1 ? COLORS.red : e.quantity < 0 ? "#ff907f" : COLORS.green} height={22} />
                    </div>
                  );
                })}
                {exposures.length > 14 && <p className="more">+{exposures.length - 14} smaller positions</p>}
              </div>
            )}
          </div>
          <div className="architecture">
            <p className="eyebrow">ENGINE ARCHITECTURE</p>
            <h2>One core. Reproducible decisions.</h2>
            <p>Trade events pass through validation, deduplication, state projection, independent controls, and an append-only audit result. The same C++20 engine that runs natively is compiled to WebAssembly and executes here.</p>
            {stress ? (
              <dl className="bench">
                <div><dt>Events</dt><dd>{integer.format(stress.events)}</dd></div>
                <div><dt>Throughput</dt><dd>{compact.format(stress.rate)}/s</dd></div>
                <div><dt>Per trade</dt><dd>{stress.nsPerEvent.toFixed(1)} ns</dd></div>
                <div><dt>Method</dt><dd>best of 3</dd></div>
              </dl>
            ) : (
              <p className="bench-hint">Run the stress test to time the native benchmark's exact workload inside WebAssembly in this browser, on a separate engine so the dashboard is untouched.</p>
            )}
          </div>
        </section>
      </main>

      {selected && <EventDrawer item={selected} limits={limits} onClose={() => setSelected(undefined)} />}

      <footer><span>SENTINEL / ENGINE v0.3</span><span>Built by Kebron Tadesse</span><span>Deterministic · Auditable · Extensible</span></footer>
    </div>
  );
}

function Metric({ icon, label, value, detail, tone, spark, unit, format = (v) => integer.format(Math.round(v)) }: {
  icon: ReactNode; label: string; value: number; detail: string; tone?: string; spark?: number[]; unit?: string; format?: (v: number) => string;
}) {
  const shown = useCountUp(value);
  return (
    <article className={`metric ${tone ?? ""}`}>
      <div className="metric-icon">{icon}</div>
      <div className="metric-body">
        <span>{label}</span>
        <strong>{format(shown)}{unit && <em>{unit}</em>}</strong>
        <small>{detail}</small>
      </div>
      {spark && <div className="metric-spark"><Sparkline values={spark} color={tone === "alert" ? COLORS.red : COLORS.green} /></div>}
    </article>
  );
}

function Decision({ item }: { item: FeedItem }) {
  if (item.outcome === "duplicate") return <span className="decision duplicate">DUPLICATE</span>;
  if (!item.alerts.length) return <span className="decision clear">CLEARED</span>;
  const tone = item.outcome === "critical" ? "critical" : "warning";
  return <span className={`decision ${tone}`}>{item.alerts[0].code.replaceAll("_", " ")}{item.alerts.length > 1 && ` +${item.alerts.length - 1}`}</span>;
}

function Rule({ code, title, value }: { code: string; title: string; value: string }) {
  return <div className="rule"><span>{code}</span><div><strong>{title}</strong><small>{value}</small></div><i /></div>;
}

function Ticker({ quotes }: { quotes: Quote[] }) {
  const row = quotes.map((q) => {
    const change = (q.price - q.open) / q.open;
    return (
      <span className="quote" key={q.symbol}>
        <b>{q.symbol}</b>
        <span>{q.price.toFixed(2)}</span>
        <em className={change >= 0 ? "up" : "down"}>{change >= 0 ? "▲" : "▼"} {(Math.abs(change) * 100).toFixed(2)}%</em>
        {RESTRICTED.includes(q.symbol) && <i>RESTRICTED</i>}
      </span>
    );
  });
  return (
    <div className="ticker" aria-hidden>
      <div className="ticker-track">{row}{row}{row}</div>
    </div>
  );
}

const STAGES = ["Event", "Validate", "Dedupe", "Rules", "Commit", "Audit"];

function Pipeline({ particles, totals, onDone }: { particles: Particle[]; totals: Totals; onDone: (id: number) => void }) {
  const counts = [totals.processed, totals.processed, totals.duplicates, totals.warnings + totals.critical, totals.committed, totals.processed];
  const captions = ["ingested", "validated", "absorbed", "signals", "committed", "records"];
  return (
    <section className="pipeline-panel panel" aria-label="Engine pipeline">
      <div className="pipeline-track">
        <div className="pipeline-rail" />
        {particles.map((p) => (
          <span key={p.id} className={`particle p-${p.outcome}`} onAnimationEnd={() => onDone(p.id)} />
        ))}
        {STAGES.map((stage, i) => (
          <div className="stage" key={stage} style={{ left: `${(i / (STAGES.length - 1)) * 100}%` }}>
            <span className="stage-node">{String(i + 1).padStart(2, "0")}</span>
            <strong>{stage}</strong>
            <small>{integer.format(counts[i])} {captions[i]}</small>
          </div>
        ))}
      </div>
    </section>
  );
}

function EventDrawer({ item, limits, onClose }: { item: FeedItem; limits: { position: number; notional: number }; onClose: () => void }) {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  const signed = item.side === "BUY" ? item.quantity : -item.quantity;
  const before = item.duplicate ? item.positionAfter : item.positionAfter - signed;
  const posUse = Math.abs(item.positionAfter) / limits.position;
  const notionalUse = item.notional / limits.notional;

  return (
    <div className="drawer-scrim" onClick={onClose}>
      <aside className="drawer" role="dialog" aria-modal="true" aria-label={`Event ${item.eventId}`} onClick={(e) => e.stopPropagation()}>
        <header>
          <div>
            <p className="eyebrow">EVENT DETAIL</p>
            <h3>{item.eventId}</h3>
          </div>
          <button className="icon-button" onClick={onClose} aria-label="Close"><X size={16} /></button>
        </header>
        <div className={`verdict v-${item.outcome}`}>
          <Decision item={item} />
          <span>{item.outcome === "clear" ? "No controls triggered" : item.outcome === "duplicate" ? "Absorbed without state change" : `${item.alerts.length} control${item.alerts.length > 1 ? "s" : ""} triggered — trade recorded, flagged for review`}</span>
        </div>
        <dl className="facts">
          <div><dt>Account</dt><dd>{item.accountId}</dd></div>
          <div><dt>Instrument</dt><dd>{item.symbol}</dd></div>
          <div><dt>Side</dt><dd>{item.side}</dd></div>
          <div><dt>Quantity</dt><dd>{integer.format(item.quantity)}</dd></div>
          <div><dt>Price</dt><dd>{item.price.toFixed(2)}</dd></div>
          <div><dt>Notional</dt><dd>{money.format(item.notional)}</dd></div>
          <div><dt>Event time</dt><dd>{new Date(item.eventTimeMs).toISOString().replace("T", " ").slice(0, 23)}</dd></div>
          <div><dt>Status</dt><dd>{item.duplicate ? "DUPLICATE" : "ACCEPTED"}</dd></div>
        </dl>

        <h4>Rule inputs</h4>
        <Gaugeline label="Position" detail={`${integer.format(before)} → ${integer.format(item.positionAfter)} of ±${integer.format(limits.position)}`} value={posUse} />
        <Gaugeline label="Notional" detail={`${money.format(item.notional)} of ${money.format(limits.notional)}`} value={notionalUse} />
        <div className="flagline">
          <span>Restricted list</span><b className={RESTRICTED.includes(item.symbol) ? "over" : ""}>{RESTRICTED.includes(item.symbol) ? "MATCH" : "no match"}</b>
        </div>

        <h4>Engine signals</h4>
        {item.alerts.length ? (
          <ul className="alert-list">
            {item.alerts.map((a: Alert, i) => (
              <li key={i} className={`sev-${a.severity.toLowerCase()}`}>
                <div><span>{RULE_META[a.code]?.code ?? "—"}</span><strong>{a.code.replaceAll("_", " ")}</strong><em>{a.severity}</em></div>
                <p>{a.message}</p>
              </li>
            ))}
          </ul>
        ) : <p className="drawer-empty">All controls passed.</p>}
        <p className="drawer-note">Messages above are produced verbatim by the C++ rule implementations.</p>
      </aside>
    </div>
  );
}

function Gaugeline({ label, detail, value }: { label: string; detail: string; value: number }) {
  return (
    <div className="gaugeline">
      <div><span>{label}</span><small>{detail}</small></div>
      <div className="gauge-track"><span className={value > 1 ? "over" : value > 0.8 ? "near" : ""} style={{ width: `${Math.min(1, value) * 100}%` }} /><i style={{ left: "100%" }} /></div>
      <b className={value > 1 ? "over" : ""}>{Math.round(value * 100)}%</b>
    </div>
  );
}
