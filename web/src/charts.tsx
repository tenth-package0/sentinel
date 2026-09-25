import { MutableRefObject, useCallback, useEffect, useMemo, useRef, useState } from "react";

export const COLORS = {
  green: "#37e39a",
  amber: "#f4c967",
  red: "#ff6f64",
  blue: "#6fb7ff",
  grid: "rgba(197, 230, 214, 0.07)",
  label: "#5d7268",
};

export const BUCKET_MS = 500;
export const WINDOW = 72;

export type Bucket = { t: number; events: number; warnings: number; critical: number };

/** Rolling time-bucketed counters, written by the engine loop and read by the canvas each frame. */
export function useFlowBuckets() {
  const ref = useRef<Bucket[]>([]);
  const record = useCallback((warnings: number, critical: number) => {
    const t = Math.floor(performance.now() / BUCKET_MS);
    const buckets = ref.current;
    let last = buckets[buckets.length - 1];
    if (!last || last.t !== t) {
      last = { t, events: 0, warnings: 0, critical: 0 };
      buckets.push(last);
      if (buckets.length > WINDOW + 4) buckets.shift();
    }
    last.events++;
    last.warnings += warnings;
    last.critical += critical;
  }, []);
  return useMemo(() => ({ ref, record }), [record]);
}

/** Rate over the trailing `seconds`, in events per second. */
export function trailingRate(buckets: Bucket[], seconds: number) {
  const now = Math.floor(performance.now() / BUCKET_MS);
  const span = Math.round((seconds * 1000) / BUCKET_MS);
  let events = 0;
  for (const b of buckets) if (now - b.t < span) events += b.events;
  return events / seconds;
}

function prepareCanvas(canvas: HTMLCanvasElement) {
  const dpr = window.devicePixelRatio || 1;
  const { width, height } = canvas.getBoundingClientRect();
  if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
  }
  const ctx = canvas.getContext("2d")!;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, width, height };
}

/**
 * Continuously scrolling event-rate chart. Rendering happens entirely in a
 * requestAnimationFrame loop off a ref, so a busy feed never re-renders React.
 */
export function FlowChart({ buckets }: { buckets: MutableRefObject<Bucket[]> }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    let frame = 0;
    let scale = 4;
    const draw = () => {
      frame = requestAnimationFrame(draw);
      const canvas = canvasRef.current;
      if (!canvas) return;
      const { ctx, width, height } = prepareCanvas(canvas);
      const padL = 34, padR = 10, padT = 14, padB = 22;
      const plotW = width - padL - padR;
      const plotH = height - padT - padB;
      ctx.clearRect(0, 0, width, height);

      const nowExact = performance.now() / BUCKET_MS;
      const now = Math.floor(nowExact);
      const frac = nowExact - now;
      const byT = new Map(buckets.current.map((b) => [b.t, b]));
      const series: Bucket[] = [];
      for (let i = WINDOW; i >= 0; i--) series.push(byT.get(now - i) ?? { t: now - i, events: 0, warnings: 0, critical: 0 });

      const peak = Math.max(4, ...series.map((b) => b.events));
      scale += (peak * 1.25 - scale) * 0.08;
      const step = plotW / (WINDOW - 1);
      const x = (i: number) => padL + (i - frac) * step;
      const y = (v: number) => padT + plotH - (v / scale) * plotH;

      ctx.font = "500 9px 'IBM Plex Mono', monospace";
      ctx.fillStyle = COLORS.label;
      ctx.strokeStyle = COLORS.grid;
      ctx.lineWidth = 1;
      for (let g = 0; g <= 3; g++) {
        const v = (scale / 3) * g;
        const gy = Math.round(y(v)) + 0.5;
        ctx.beginPath();
        ctx.moveTo(padL, gy);
        ctx.lineTo(width - padR, gy);
        ctx.stroke();
        ctx.fillText(String(Math.round(v * (1000 / BUCKET_MS))), 4, gy + 3);
      }
      for (let s = 0; s <= 30; s += 10) {
        const gx = padL + plotW - (s * 1000 / BUCKET_MS) * step;
        ctx.fillText(s === 0 ? "now" : `-${s}s`, gx - (s === 0 ? 18 : 10), height - 6);
      }

      ctx.save();
      ctx.beginPath();
      ctx.rect(padL, 0, plotW, height);
      ctx.clip();

      // Signal bars sit behind the rate line.
      const barW = Math.max(2, step * 0.55);
      series.forEach((b, i) => {
        const cx = x(i) - barW / 2;
        if (b.warnings) {
          ctx.fillStyle = "rgba(244, 201, 103, 0.35)";
          ctx.fillRect(cx, y(b.warnings + b.critical), barW, y(0) - y(b.warnings + b.critical));
        }
        if (b.critical) {
          ctx.fillStyle = "rgba(255, 111, 100, 0.75)";
          ctx.fillRect(cx, y(b.critical), barW, y(0) - y(b.critical));
        }
      });

      const path = new Path2D();
      series.forEach((b, i) => {
        const px = x(i), py = y(b.events);
        if (i === 0) path.moveTo(px, py);
        else {
          const prevX = x(i - 1), prevY = y(series[i - 1].events);
          const mid = (prevX + px) / 2;
          path.bezierCurveTo(mid, prevY, mid, py, px, py);
        }
      });
      const area = new Path2D(path);
      area.lineTo(x(series.length - 1), y(0));
      area.lineTo(x(0), y(0));
      area.closePath();
      const gradient = ctx.createLinearGradient(0, padT, 0, padT + plotH);
      gradient.addColorStop(0, "rgba(55, 227, 154, 0.32)");
      gradient.addColorStop(1, "rgba(55, 227, 154, 0)");
      ctx.fillStyle = gradient;
      ctx.fill(area);
      ctx.strokeStyle = COLORS.green;
      ctx.lineWidth = 1.6;
      ctx.shadowColor = "rgba(55, 227, 154, 0.6)";
      ctx.shadowBlur = 8;
      ctx.stroke(path);
      ctx.restore();

      const lastY = y(series[series.length - 1].events);
      const pulse = 3 + Math.sin(performance.now() / 180) * 1.2;
      ctx.fillStyle = COLORS.green;
      ctx.beginPath();
      ctx.arc(x(series.length - 1), lastY, 2.5, 0, Math.PI * 2);
      ctx.fill();
      ctx.strokeStyle = "rgba(55, 227, 154, 0.35)";
      ctx.beginPath();
      ctx.arc(x(series.length - 1), lastY, pulse + 3, 0, Math.PI * 2);
      ctx.stroke();
    };
    draw();
    return () => cancelAnimationFrame(frame);
  }, [buckets]);

  return <canvas ref={canvasRef} className="flow-canvas" aria-label="Event rate and signals over the last 36 seconds" role="img" />;
}

/** Small inline SVG sparkline; transitions come from CSS on the path. */
export function Sparkline({ values, color = COLORS.green, height = 26 }: { values: number[]; color?: string; height?: number }) {
  const width = 100;
  if (values.length < 2) return <svg className="sparkline" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" />;
  const min = Math.min(...values), max = Math.max(...values);
  const span = max - min || 1;
  const points = values.map((v, i) => `${(i / (values.length - 1)) * width},${height - 2 - ((v - min) / span) * (height - 4)}`);
  return (
    <svg className="sparkline" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" aria-hidden>
      <polyline points={`0,${height} ${points.join(" ")} ${width},${height}`} fill={color} opacity={0.1} />
      <polyline points={points.join(" ")} fill="none" stroke={color} strokeWidth={1.4} vectorEffect="non-scaling-stroke" />
    </svg>
  );
}

export type Slice = { key: string; label: string; value: number; color: string };

/** Animated donut; each arc's dash length transitions when counts change. */
export function Donut({ slices, total }: { slices: Slice[]; total: number }) {
  const radius = 52;
  const circumference = 2 * Math.PI * radius;
  const sum = slices.reduce((s, x) => s + x.value, 0);
  let offset = 0;
  return (
    <svg className="donut" viewBox="0 0 140 140" role="img" aria-label="Signals by rule">
      <circle cx="70" cy="70" r={radius} fill="none" stroke="rgba(197,230,214,.07)" strokeWidth="13" />
      {slices.map((slice) => {
        const length = sum ? (slice.value / sum) * circumference : 0;
        const gap = length > 4 ? 2.5 : 0;
        const el = (
          <circle
            key={slice.key}
            cx="70" cy="70" r={radius}
            fill="none"
            stroke={slice.color}
            strokeWidth="13"
            strokeDasharray={`${Math.max(0, length - gap)} ${circumference}`}
            strokeDashoffset={-offset}
            transform="rotate(-90 70 70)"
            className="donut-arc"
          />
        );
        offset += length;
        return el;
      })}
      <text x="70" y="67" textAnchor="middle" className="donut-total">{total}</text>
      <text x="70" y="84" textAnchor="middle" className="donut-caption">SIGNALS</text>
    </svg>
  );
}

/** Eases a displayed number toward its target. */
export function useCountUp(target: number, duration = 450) {
  const [value, setValue] = useState(target);
  const from = useRef(target);
  useEffect(() => {
    const start = performance.now();
    const origin = from.current;
    let frame = 0;
    const tick = (now: number) => {
      const p = Math.min(1, (now - start) / duration);
      const eased = 1 - Math.pow(1 - p, 3);
      const next = origin + (target - origin) * eased;
      from.current = next;
      setValue(next);
      if (p < 1) frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [target, duration]);
  return value;
}
