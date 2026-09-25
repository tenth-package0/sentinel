export type Alert = {
  code: string;
  severity: "INFO" | "WARNING" | "CRITICAL";
  message: string;
};

export type EngineResult = {
  eventId: string;
  duplicate: boolean;
  positionAfter: number;
  alerts: Alert[];
  error?: string;
};

export type Position = { accountId: string; symbol: string; quantity: number };

export type TradeInput = {
  eventId: string;
  accountId: string;
  symbol: string;
  side: "BUY" | "SELL";
  quantity: number;
  price: number;
  eventTimeMs: number;
};

type WasmModule = {
  ccall: (
    name: string,
    returnType: "string" | "number" | null,
    argumentTypes: string[],
    args: Array<string | number>,
  ) => string | number | null;
};

let modulePromise: Promise<WasmModule> | undefined;

async function loadModule(): Promise<WasmModule> {
  if (!modulePromise) {
    const loaderPath = "/wasm/sentinel.js";
    const dynamicImport = new Function("path", "return import(path)") as (
      path: string,
    ) => Promise<{ default: (options: object) => Promise<WasmModule> }>;
    modulePromise = dynamicImport(loaderPath).then(
      ({ default: createSentinelModule }) =>
        createSentinelModule({
          locateFile: (path: string) => `/wasm/${path}`,
        }),
    );
  }
  return modulePromise;
}

export class SentinelClient {
  private constructor(private readonly module: WasmModule) {}

  static async create() {
    const module = await loadModule();
    const client = new SentinelClient(module);
    client.configure(1_000, 250_000);
    return client;
  }

  configure(positionLimit: number, notionalLimit: number) {
    this.module.ccall(
      "sentinel_configure",
      null,
      ["number", "number"],
      [positionLimit, notionalLimit],
    );
  }

  process(trade: TradeInput): EngineResult {
    const raw = this.module.ccall(
      "sentinel_process",
      "string",
      ["string", "string", "string", "number", "number", "number", "number"],
      [
        trade.eventId,
        trade.accountId,
        trade.symbol,
        trade.side === "BUY" ? 0 : 1,
        trade.quantity,
        trade.price,
        trade.eventTimeMs,
      ],
    ) as string;
    return JSON.parse(raw) as EngineResult;
  }

  /**
   * Runs the native benchmark's workload inside WebAssembly on a separate,
   * preallocated engine (dashboard state is untouched). Returns ns per trade.
   */
  benchmark(trades: number): number {
    return this.module.ccall("sentinel_benchmark", "number", ["number"], [trades]) as number;
  }

  positions(): Position[] {
    const raw = this.module.ccall("sentinel_positions", "string", [], []) as string;
    return JSON.parse(raw) as Position[];
  }

  replay() {
    const raw = this.module.ccall("sentinel_replay", "string", [], []) as string;
    return JSON.parse(raw) as { replayed: number; processed: number };
  }

  reset() {
    this.module.ccall("sentinel_reset", null, [], []);
  }
}
