import create from "zustand";
import { subscribeToStream, fetchChain, fetchStatus } from "../lib/api";

export interface KolibriPayload {
  step: number;
  eff: number;
  compl: number;
  fa: string;
  run_id: string;
  eff_val: number;
  eff_train: number;
  eff_test: number;
  explain: string;
  votes: number[];
  cfg_hash: string;
  prev: string;
  hmac_alg: string;
  salt: string;
}

export interface KolibriBlock {
  payload: KolibriPayload;
  hash: string;
  hmac: string;
}

interface ChainState {
  status: {
    version: string;
    run_id: string;
    time_utc: string;
    fmt: number;
  } | null;
  blocks: KolibriBlock[];
  effHistory: Array<{ step: number; eff: number }>;
  complHistory: Array<{ step: number; compl: number }>;
  online: boolean;
  initialized: boolean;
  connect: () => void;
  refresh: () => Promise<void>;
  setOnline: (value: boolean) => void;
}

export const useAppStore = create<ChainState>((set, get) => ({
  status: null,
  blocks: [],
  effHistory: [],
  complHistory: [],
  online: true,
  initialized: false,
  setOnline: (value) => set({ online: value }),
  connect: () => {
    if (get().initialized) return;
    set({ initialized: true });
    fetchStatus().then((status) => set({ status })).catch(() => set({ online: false }));
    fetchChain(50)
      .then((blocks) =>
        set({
          blocks,
          effHistory: blocks.map((b) => ({ step: b.payload.step, eff: b.payload.eff_val })),
          complHistory: blocks.map((b) => ({ step: b.payload.step, compl: b.payload.compl }))
        })
      )
      .catch(() => set({ online: false }));
    subscribeToStream({
      onBlock: (event) => {
        set((state) => {
          const nextBlocks = [...state.blocks];
          const existing = nextBlocks.find((b) => b.payload.step === event.step);
          if (!existing) {
            nextBlocks.push({
              payload: {
                step: event.step,
                eff: event.eff,
                compl: event.compl,
                fa: event.fa,
                run_id: state.status?.run_id ?? "",
                eff_val: event.eff,
                eff_train: event.eff,
                eff_test: event.eff,
                explain: "streamed",
                votes: [],
                cfg_hash: "",
                prev: "",
                hmac_alg: "",
                salt: ""
              },
              hash: event.hash,
              hmac: ""
            });
          }
          return {
            blocks: nextBlocks.sort((a, b) => a.payload.step - b.payload.step),
            effHistory: [...state.effHistory, { step: event.step, eff: event.eff }],
            complHistory: [...state.complHistory, { step: event.step, compl: event.compl }]
          };
        });
      },
      onVerify: (result) => {
        if (!result.ok) {
          console.warn("verify event", result.reason);
        }
      },
      onMetric: (metric) => {
        set((state) => ({
          effHistory: [...state.effHistory, { step: state.effHistory.length, eff: metric.eff_val }],
          complHistory: [...state.complHistory, { step: state.complHistory.length, compl: metric.compl }]
        }));
      },
      onStatus: (online) => set({ online })
    });
  },
  refresh: async () => {
    const [status, blocks] = await Promise.all([fetchStatus(), fetchChain(50)]);
    set({
      status,
      blocks,
      effHistory: blocks.map((b) => ({ step: b.payload.step, eff: b.payload.eff_val })),
      complHistory: blocks.map((b) => ({ step: b.payload.step, compl: b.payload.compl }))
    });
  }
}));
