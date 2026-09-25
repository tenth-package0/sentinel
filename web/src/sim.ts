import { TradeInput } from "./engine";

export const ACCOUNTS = ["ALPHA-7", "BETA-2", "DELTA-4", "GAMMA-9", "OMEGA-1"];

const BASE_PRICES: Record<string, number> = {
  AAPL: 191.42,
  MSFT: 418.16,
  NVDA: 178.33,
  TSLA: 243.9,
  AMZN: 186.05,
  META: 512.3,
  GME: 24.72,
};

export const SYMBOLS = Object.keys(BASE_PRICES);

export type Quote = { symbol: string; price: number; open: number };

/** Random-walk price book shared by the ticker and the live feed generator. */
export class MarketSim {
  private prices = new Map(Object.entries(BASE_PRICES));
  private sequence = 1;
  private recent: TradeInput[] = [];

  quotes(): Quote[] {
    return SYMBOLS.map((symbol) => ({
      symbol,
      price: this.prices.get(symbol)!,
      open: BASE_PRICES[symbol],
    }));
  }

  tick() {
    for (const [symbol, price] of this.prices) {
      const drift = (BASE_PRICES[symbol] - price) * 0.02;
      this.prices.set(symbol, Math.max(1, price + drift + price * (Math.random() - 0.5) * 0.004));
    }
  }

  /**
   * Produces a plausible order flow. Sides mean-revert toward flat so the book
   * hovers near (and occasionally through) the position limit, and a small share
   * of events are deliberately duplicated or back-dated to exercise the engine.
   */
  next(position: (account: string, symbol: string) => number, limit: number): TradeInput {
    const roll = Math.random();
    if (roll < 0.03 && this.recent.length) {
      return this.recent[Math.floor(Math.random() * this.recent.length)];
    }

    const symbol = Math.random() < 0.035 ? "GME" : SYMBOLS[Math.floor(Math.random() * (SYMBOLS.length - 1))];
    const accountId = ACCOUNTS[Math.floor(Math.random() * ACCOUNTS.length)];
    const held = position(accountId, symbol);
    const pressure = Math.max(-0.45, Math.min(0.45, held / (limit * 1.6)));
    const side = Math.random() < 0.5 - pressure ? "BUY" : "SELL";
    const large = Math.random() < 0.06;
    const quantity = large ? 500 + Math.floor(Math.random() * 700) : 10 + Math.floor(Math.random() * 260);
    const price = Number(this.prices.get(symbol)!.toFixed(2));
    const lateBy = roll > 0.97 ? 4_000 + Math.floor(Math.random() * 6_000) : 0;

    const trade: TradeInput = {
      eventId: `TRD-${(20_000 + this.sequence).toString()}`,
      accountId,
      symbol,
      side,
      quantity,
      price,
      eventTimeMs: Date.now() - lateBy,
    };
    this.sequence++;
    this.recent.push(trade);
    if (this.recent.length > 40) this.recent.shift();
    return trade;
  }
}
