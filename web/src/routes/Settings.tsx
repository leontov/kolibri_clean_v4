import { useState } from "react";
import { showInstallPrompt } from "../lib/pwa";

export default function Settings() {
  const [telemetry, setTelemetry] = useState(false);
  const [privateMode, setPrivateMode] = useState(true);

  return (
    <section className="space-y-4">
      <h2 className="text-xl font-semibold">Settings & Privacy</h2>
      <div className="card space-y-4">
        <div className="flex items-center justify-between">
          <div>
            <h3 className="font-semibold">Private mode</h3>
            <p className="text-sm text-slate-400">Keep chain metrics on device until verified.</p>
          </div>
          <input type="checkbox" checked={privateMode} onChange={() => setPrivateMode((value) => !value)} className="h-5 w-5" />
        </div>
        <div className="flex items-center justify-between">
          <div>
            <h3 className="font-semibold">Anonymous telemetry</h3>
            <p className="text-sm text-slate-400">Share aggregated uptime stats to improve FA-10.</p>
          </div>
          <input type="checkbox" checked={telemetry} onChange={() => setTelemetry((value) => !value)} className="h-5 w-5" />
        </div>
        <button
          type="button"
          onClick={() => showInstallPrompt()}
          className="rounded-lg border border-brand-500/60 px-4 py-2 text-sm text-brand-100 hover:bg-brand-500/20"
        >
          Install Kolibri PWA
        </button>
      </div>
      <div className="card text-sm text-slate-400">
        <p>
          Payload evolution policy: append-only fields at the tail of the canonical payload schema. Existing keys remain stable for
          deterministic verification.
        </p>
      </div>
    </section>
  );
}
